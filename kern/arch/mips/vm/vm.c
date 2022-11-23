/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <bitmap.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/iovec.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <stat.h>
#include <uio.h>
#include <vfs.h>
#include <vm.h>
#include <synch.h>
#include "opt-vm_perf.h"

// At boot coremap is disabled until it has been initialized.
static int coremap_enabled = 0;

// Coremap lock must be a spinlock because we need to call kfree from
// interrupt handlers which can deadlock if they go to sleep.
static struct spinlock coremap_lock;

// Acquire coremap_lock before accessing any of these shared variables.
static paddr_t firstpaddr;  // First byte that can be allocated.
static paddr_t lastpaddr;   // Last byte that can be allocated.
static struct core_page *coremap;
static unsigned used_bytes;
static unsigned page_max;  // Total number of allocatable pages.
static unsigned next_fit;  // Coremap index to resume free page search.

// Swap system globals.
#define SWAP_PATH "lhd0raw:"  
static struct bitmap *swapmap;
static struct lock *swapmap_lock;
static struct vnode *swapdisk_vn;
static struct lock *swapdisk_lock;
static size_t swapdisk_pages;
static int swap_enabled = 0;  // Swap is only enabled if swap disk is found.

// Tracks when TLB shootdowns complete.
static struct semaphore *tlbshootdown_sem;

#if OPT_VM_PERF
static unsigned tlb_faults = 0;
static unsigned swap_ins = 0;
static unsigned swap_outs = 0;
static unsigned faults = 0;
static unsigned evictions = 0;

static struct spinlock vm_perf_lock;

static void init_vm_perf() {
	spinlock_init(&vm_perf_lock);
	reset_vm_perf();
}

void reset_vm_perf() {
    spinlock_acquire(&vm_perf_lock);
	tlb_faults = 0;
	swap_ins = 0;
	swap_outs = 0;
	faults = 0;
	evictions = 0;
	spinlock_release(&vm_perf_lock);
}

void count_tlb_fault() { 
	spinlock_acquire(&vm_perf_lock);
	tlb_faults++;
	spinlock_release(&vm_perf_lock);
}

void count_swap_in() {
	spinlock_acquire(&vm_perf_lock);
	swap_ins++;
	spinlock_release(&vm_perf_lock);
}

void count_swap_out() {
	spinlock_acquire(&vm_perf_lock);
	swap_outs++;
	spinlock_release(&vm_perf_lock);
}

void count_fault() {
	spinlock_acquire(&vm_perf_lock);
	faults++;
	spinlock_release(&vm_perf_lock);
}

void count_eviction() {
	spinlock_acquire(&vm_perf_lock);
	evictions++;
	spinlock_release(&vm_perf_lock);
}

void dump_vm_perf() {
	spinlock_acquire(&vm_perf_lock);
	kprintf("tlb_faults = %8d\n", tlb_faults);
	kprintf("swap_ins   = %8d\n", swap_ins);
	kprintf("swap_outs  = %8d\n", swap_outs);
	kprintf("evictions  = %8d\n", evictions);
	kprintf("faults     = %8d\n", faults);
	spinlock_release(&vm_perf_lock);
}
#endif

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
 * lock coremap outside this module.
 *
 */
void spinlock_acquire_coremap() {
	spinlock_acquire(&coremap_lock);
}

/*
 * Release coremap lock outside this module.
 */
void spinlock_release_coremap() {
	spinlock_release(&coremap_lock);
}

/*
 * Enable/disable swap.
 *
 * Only intended for testing to selectively enable/disable
 * swap.  Do not use in actual operation as enabling swap
 * without running vm_boostrap will not work.  Also
 * disabling swap in the middle of operation will corrupt
 * memory.
 * 
 * Args:
 *   enabled: 0 = disable swap, 1 = enable swap.
 * 
 * Returns:
 *   Previous enabled state.
 */
int
set_swap_enabled(int enabled)
{
	int old_state;

	lock_acquire(swapdisk_lock);
	old_state = swap_enabled;
	swap_enabled = enabled;
	lock_release(swapdisk_lock);
	return old_state;
}

/*
 * Marks block_index free in swapmap.
 */
void
free_swapmap_block(int block_index)
{
	lock_acquire(swapmap_lock);
	bitmap_unmark(swapmap, block_index);
	lock_release(swapmap_lock);
}

/*
 * Returns number of used pages on swap disk.
 */
size_t
swap_used_pages()
{
	size_t p;
	size_t used = 0;

	KASSERT(swap_enabled);
	lock_acquire(swapmap_lock);
	for (p = 0; p < swapdisk_pages; p++) {
        if (bitmap_isset(swapmap, p)) {
			used++;
		}
	}
	lock_release(swapmap_lock);
	return used;
}

static unsigned get_core_npages(unsigned page_index)
{
    return (coremap[page_index].status) & VM_CORE_NPAGES;
}

static unsigned set_core_status(int used, int accessed, int dirty, unsigned npages)
{
    return (used ? VM_CORE_USED : 0) | 
           (accessed ? VM_CORE_ACCESSED : 0) | 
           (dirty ? VM_CORE_DIRTY : 0) |
           (npages & VM_CORE_NPAGES);
}

/*
 * Print contents of coremap for deubbing.
 * Caller is responsible for locking coremap.
 */
static void 
dump_coremap()
{
	unsigned p;
	unsigned npages;
	unsigned status;
	
	KASSERT(spinlock_do_i_hold(&coremap_lock));
	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		status = coremap[p].status;
		kprintf("coremap[%3u]: status=0x%08x, paddr=0x%08x, pte=0x%08x, vaddr=0x%08x, npages=%u\n", 
		  p, status, core_idx_to_paddr(p), (vaddr_t)coremap[p].pte, coremap[p].vaddr, npages);
	}
}

void lock_and_dump_coremap()
{
    spinlock_acquire(&coremap_lock);
	dump_coremap();
	spinlock_release(&coremap_lock);
}

/*
 * Converts coremap index to physical address.
 */
paddr_t
core_idx_to_paddr(unsigned p)
{
    return (paddr_t)(p * PAGE_SIZE);
}

/* 
 * Converts physical address to coremap index.
 */
unsigned
paddr_to_core_idx(paddr_t paddr)
{
    return (unsigned)(paddr / PAGE_SIZE);
}

/*
 * Returns virtual address mapped to paddr.
 */
vaddr_t
vm_get_vaddr(paddr_t paddr)
{
    unsigned p;
    vaddr_t vaddr;

	KASSERT(spinlock_do_i_hold(&coremap_lock));
	p = paddr_to_core_idx(paddr);
	vaddr = coremap[p].vaddr;
	return vaddr;
}

/*
 * Invalidates one entry in TLB.
 *
 */
void 
vm_tlb_remove(vaddr_t vaddr)
{
	uint32_t ehi;
	int spl;
	int idx;

	spl = splhigh();
	ehi = vaddr & PAGE_FRAME;
	idx = tlb_probe(ehi, 0);
	if (idx >= 0) {
        tlb_write(TLBHI_INVALID(idx), TLBLO_INVALID(), idx);
	}
	splx(spl);
}

/*
 * Invalidates all entries in TLB.
 *
 */
void 
vm_tlb_erase()
{
	int spl;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Sanity check to confirm coremap appears valid.
 *
 * Caller is responsible for locking coremap.
 *
 * Returns:
 *   0 if coremap is valid, else panics.
 */
int
validate_coremap()
{
	unsigned p;
	unsigned used_pages = 0;
	unsigned free_pages = 0;
	unsigned npages;
    unsigned status;

	KASSERT(spinlock_do_i_hold(&coremap_lock));
    KASSERT(next_fit < page_max);
	for (p = 0; p < page_max; p += npages) {
		if (p > 0) {
			KASSERT(coremap[p].prev == p - npages);
		}
		npages = get_core_npages(p);
		status = coremap[p].status;
        if (status & VM_CORE_USED) {
			used_pages += npages;
			if (coremap[p].pte == NULL) {
				// Kernel page.
                KASSERT(coremap[p].vaddr >= MIPS_KSEG0);
                KASSERT(coremap[p].vaddr < MIPS_KSEG1);
            } else {
				// User page.
                KASSERT(coremap[p].vaddr < MIPS_KSEG0);
				// We only support allocating user pages one at a time.
				KASSERT(npages == 1);
            }
		} else {
			free_pages += npages;
		}
	}
	if (used_pages * PAGE_SIZE != used_bytes) {
		kprintf("used_pages = %u\n", used_pages);
		kprintf("used_bytes = %u\n", used_bytes);
		dump_coremap();
		panic("(used_pages * PAGE_SIZE) (%u) != used_bytes (%u)",
		  used_pages * PAGE_SIZE, used_bytes);
	}
	if (used_pages + free_pages != page_max) {
		kprintf("used_pages = %u\n", used_pages);
		kprintf("free_pages = %u\n", free_pages);
		kprintf("page_max = %u\n", page_max);
		dump_coremap();
		panic("(used_pages + free_pages) (%u) != page_max (%u)",
		  used_pages + free_pages, page_max);
	}

	return 0;
}

/*
 * Initializes the physical to virtual memory map.
 *
 * Must be run after ram_bootstrap().
 */
void
vm_init_coremap()
{
	size_t total_bytes;  // total bytes in physical memory.
	size_t coremap_bytes;
	paddr_t kernel_top;  // Immediately above kernel code.
	paddr_t coremap_paddr;
	unsigned p;

	// Optional: dynamic data structures can be kmalloc'd here
	// before the coremap is enabled.  Any pre-coremap allocated
	// memory, however, will never be freed.

	lastpaddr = ram_getsize();
	// RAM initialization can only occur once and locks in any
	// memory allocated with ram_stealmem().  We cannot make
	// further calls to ram_stealmem().
	kernel_top = ram_getfirstfree();
	KASSERT((kernel_top & PAGE_FRAME) == kernel_top);

	// Total memory in bytes minus the kernel code.
	total_bytes = lastpaddr;
	page_max = paddr_to_core_idx(total_bytes);
	coremap_bytes = page_max * sizeof(struct core_page);
	coremap_paddr = kernel_top;

	// First allocatable page is above coremap and page aligned up.
	firstpaddr = coremap_paddr + coremap_bytes;
	firstpaddr = (firstpaddr + PAGE_SIZE - 1) & PAGE_FRAME;

	// From this point, we will be accessing coremap directly, so
	// convert to a direct mapped virtual address and zero out.
	coremap = (struct core_page *)PADDR_TO_KVADDR(coremap_paddr);
	bzero((void *)coremap, coremap_bytes);	
	// Mark kernel and coremap pages as allocated in coremap.
	p = paddr_to_core_idx(firstpaddr);
	coremap[0].status = set_core_status(1, 0, 0, p);
	coremap[0].pte = NULL;
	coremap[0].vaddr = (vaddr_t)MIPS_KSEG0;
	coremap[0].prev = 0;
	// Mark remainder of pages as one big free cluster.
	KASSERT(p < page_max);
	coremap[p].status = set_core_status(0, 0, 0, page_max - p);
	coremap[p].pte = NULL;
	coremap[p].vaddr = (vaddr_t)MIPS_KSEG0;
	coremap[p].prev = 0;
	next_fit = p;
	// Includes kernel and coremap in used_bytes.
	used_bytes = p * PAGE_SIZE;

	// Switch from ram_stealmem allocater to coremap.
	coremap_enabled = 1;

	spinlock_acquire(&coremap_lock);
	KASSERT(validate_coremap() == 0);
	spinlock_release(&coremap_lock);

	kprintf("\nvm_init_coremap\n");
	kprintf("lastpaddr  = 0x%08x\n", lastpaddr);
	kprintf("firstpaddr = 0x%08x\n", firstpaddr);
	kprintf("coremap    = 0x%08x\n", coremap_paddr);
	kprintf("page_max   = %u\n", page_max);
	kprintf("\n");
}

/*
 * Initializes virtual memory swap system at boot.
 */
void
vm_bootstrap(void)
{
    char vfs_path[PATH_MAX];
    const mode_t unused_mode = 0x777;
	struct stat statbuf;
    int result;

	// vfs_open destructively uses filepath, so pass in a copy.
	strcpy(vfs_path, SWAP_PATH);
	result = vfs_open(vfs_path, O_RDWR, unused_mode, &swapdisk_vn);
	if (result) {
		kprintf("Swap DISABLED.\n");
        swap_enabled = 0;
        return;
	}
	kprintf("Swap ENABLED.\n");
	swap_enabled = 1;
	result = VOP_STAT(swapdisk_vn, &statbuf);
	swapdisk_pages = (int)(statbuf.st_size / PAGE_SIZE);
	swapmap = bitmap_create(swapdisk_pages);
	if (swapmap == NULL) {
		vfs_close(swapdisk_vn);		
		panic("vm_bootstrap: Cannot create swapmap.");
	}
	swapmap_lock = lock_create("swapmap");
	if (swapmap_lock == NULL) {
		bitmap_destroy(swapmap);
		vfs_close(swapdisk_vn);
		panic("vm_bootstrap: Cannot create swapmap lock.");
	}
	swapdisk_lock = lock_create("swapdisk");
	if (swapdisk_lock == NULL) {
		lock_destroy(swapmap_lock);
		bitmap_destroy(swapmap);
		vfs_close(swapdisk_vn);
		panic("vm_bootstrap: Cannot create swapdisklock.");
	}
	kprintf("Total swapdisk pages %u\n", swapdisk_pages);

	tlbshootdown_sem = sem_create("tlbshootdown", 0);
	if (tlbshootdown_sem == NULL) {
        panic("vm_bootstrap: Could not create tlbshootdown_sem");
    }
#if OPT_VM_PERF
    init_vm_perf();
#endif
}

/*
 * Read a page from swap disk into physical memory.
 * 
 * Args:
 *   block_index: Page number offset tor read (same as swapmap).
 *   paddr: Physical address to store data.
 *
 * Returns:
 *   0 on success, else 1 on error.
 */
int 
block_read(unsigned block_index, paddr_t paddr)
{
    struct iovec iov;
	struct uio my_uio;
	off_t offset;
	void *buf;
	int result;

	KASSERT(swap_enabled);
	KASSERT((paddr >= firstpaddr) && (paddr <= lastpaddr));
	KASSERT(swapdisk_pages > 0);
	KASSERT(block_index < swapdisk_pages);

#if OPT_VM_PERF
    count_swap_in();
#endif

	lock_acquire(swapmap_lock);
	KASSERT(bitmap_isset(swapmap, block_index));
	lock_release(swapmap_lock);

	offset = block_index * PAGE_SIZE;
	buf = (void *)PADDR_TO_KVADDR(paddr);
	uio_kinit(&iov, &my_uio, buf, PAGE_SIZE, offset, UIO_READ);

	lock_acquire(swapdisk_lock);
	result = VOP_READ(swapdisk_vn, &my_uio);
	lock_release(swapdisk_lock);

	return (result || (my_uio.uio_resid != 0));
}

/*
 * Write a page to swap disk from physical memory.
 * 
 * Args:
 *   block_index: Page number offset tor write (same as swapmap).
 *   paddr: Physical address to read data.
 *
 * Returns:
 *   0 on success, else 1 on error.
 */
int 
block_write(unsigned block_index, paddr_t paddr)
{
    struct iovec iov;
	struct uio my_uio;
	off_t offset;
	void *buf;
	int result;

	KASSERT(swap_enabled);
	KASSERT((paddr >= firstpaddr) && (paddr <= lastpaddr));
	KASSERT(swapdisk_pages > 0);
	KASSERT(block_index < swapdisk_pages);

#if OPT_VM_PERF
    count_swap_out();
#endif

	lock_acquire(swapmap_lock);
	KASSERT(bitmap_isset(swapmap, block_index));
	lock_release(swapmap_lock);

	offset = block_index * PAGE_SIZE;
	buf = (void *)PADDR_TO_KVADDR(paddr);
	uio_kinit(&iov, &my_uio, buf, PAGE_SIZE, offset, UIO_WRITE);

	lock_acquire(swapdisk_lock);
	result = VOP_WRITE(swapdisk_vn, &my_uio);
	lock_release(swapdisk_lock);

	return (result || (my_uio.uio_resid != 0));
}

/*
 * Selects a page for eviction from the coremap.
 *
 * Implements the eviction policy for the virtual memory
 * system.  This is a read-only operation.  Will not
 * evict any kernel-owned pages.
 * 
 * Caller is responsible for locking coremap.
 *
 * Returns:
 *   coremap index of page to evict, else 0 if no evictable pages.
 */

#define EVICT_CLOCK 0
#if EVICT_CLOCK
// CLOCK page replacement index into coremap.
static unsigned next_evict = 0;

// CLOCK page replacement policy.
// Sweeps through coremap and takes first page that has not been
// accessed since last sweep.  Or takes next_evict index if all
// have been accessed.  Note MIPS does not have hardware marking
// of accessed pages, so kernel cannot see TLB hit statistics.
// Thus, a page with one TLB miss will get "accessed" but a
// page with many TLB hits does not register any accesses.
static unsigned
find_victim_page()
{
    unsigned p;  // Page index into coremap.
	unsigned cluster_pages;

    KASSERT(spinlock_do_i_hold(&coremap_lock));

    // CLOCK replacement policy.
	p = next_evict; 
	while ((coremap[p].pte == NULL) || 
	      ((coremap[p].status & VM_CORE_ACCESSED) &&
	       (coremap[p].status & VM_CORE_USED))) {
        coremap[p].status &= ~VM_CORE_ACCESSED;
		cluster_pages = get_core_npages(p);
		p = (p + cluster_pages) % page_max;
	}
	KASSERT(coremap[p].pte != NULL);
	cluster_pages = get_core_npages(p);
	next_evict = (p + cluster_pages) % page_max;
	return p;
}
#endif

#define EVICT_RANDOM 1
#if EVICT_RANDOM
static unsigned
find_victim_page()
{
	unsigned p;
	unsigned npages;
	unsigned victim;
	unsigned user_pages = 0;
	
	KASSERT(spinlock_do_i_hold(&coremap_lock));

	// Use two passes to select a random page.  One could use reservoir
	// sampling to do this in one pass at the expense of generating more
	// random numbers.  Since the coremap is guaranteed to be resident in 
	// memory two passes should be fast enough.

	// Pass 1: Inventory available user pages.
	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		// First choice are pages that became free since we last checked.
		if (!(coremap[p].status & VM_CORE_USED)) {
			return p;
		}
		if (coremap[p].pte == NULL) {
			// Skip kernel pages.
			continue;
		}
		if (lock_do_i_hold(coremap[p].pte->lock)) {
			// Skip pages that are locked.
			continue;
		}
		user_pages++;
	}
	if (user_pages == 0) {
        return 0;
	}
	// Pass 2: Choose one of the occupied user pages at random.
	victim = random() % user_pages;
	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		if (coremap[p].pte == NULL) {
			// Skip kernel pages.
			continue;
		}
		if (lock_do_i_hold(coremap[p].pte->lock)) {
			// Skip pages that are locked.
			continue;
		}
		if (victim == 0) {
			return p;
		}
		victim--;
	}
	panic("find_victim_page: failed to find victim page.");
}
#endif


/*
 * Save page to disk if needed.
 *
 * Args:
 *   pte: Pointer to page table entry of page to maybe swap out.
 *   dirty: non-zero if page has been modified, else 0.
 * 
 * Returns:
 *   0 on success else errno.
 */
int
save_page(struct pte *pte, int dirty) {
	unsigned block_index;
	int result;

	KASSERT(pte != NULL);
	KASSERT(lock_do_i_hold(pte->lock));

	// TODO(aabo): Can we eliminate VM_PTE_BACKED by intiially marking all pages as dirty?
	
	if (!(pte->status & VM_PTE_BACKED)) {
        lock_acquire(swapmap_lock);
        result = bitmap_alloc(swapmap, &block_index);
		if (result) {
			lock_release(pte->lock);
			lock_release(swapmap_lock);
			return result;
		}
        lock_release(swapmap_lock);
        pte->block_index = block_index;
	}
	if (dirty || !(pte->status & VM_PTE_BACKED)) {
        result = block_write(pte->block_index, pte->paddr);
        if (result) {
			lock_release(pte->lock);
            return ENOSPC;
        }
	}
	pte->status |= VM_PTE_BACKED;
	return 0;
}

/*
 * Finds and evicts a userspace page.
 *
 * Args:
 *   paddr: pointer to physical address of freed page.
 *
 * Returns:
 *   0 on success, else errno value.
 */
int
evict_page(paddr_t *paddr)
{
	// "old" refers to page to be evicted.
	struct core_page old_core;
	int dirty;
	paddr_t kvaddr;
	int p;
	struct tlbshootdown shootdown;
	int result;

	spinlock_acquire(&coremap_lock);
	// Identify a page to evict.
	p = find_victim_page();
	if (p == 0) {
		// No evictable pages.
		spinlock_release(&coremap_lock);
		return ENOMEM;
	}
	old_core = coremap[p];
	// Allocate to kernel before releasing coremap to prevent
	// another process from taking it.
	*paddr = coremap_assign_to_kernel(p, 1);
	if (!(old_core.status & VM_CORE_USED)) {
		// New already free page (not actually an eviction).
		used_bytes += PAGE_SIZE;
	}
	spinlock_release(&coremap_lock);

	lock_acquire(old_core.pte->lock);
	if (old_core.status & VM_CORE_USED) {
#if OPT_VM_PERF
        count_eviction();
#endif
        // We assume we are evicting exactly one page.
        KASSERT((old_core.status & VM_CORE_NPAGES) == 1);
		
		// Deactivate page so it is not accessed during page out.
        // Once removed from TLB, any page faults will block
		// waiting for old_core.pte->lock until we are done.
        shootdown.vaddr = old_core.vaddr;
		shootdown.sem = tlbshootdown_sem;
		vm_tlb_remove(old_core.vaddr);
		ipi_broadcast_tlbshootdown(&shootdown);
		// Refresh page dirty status in case page was modified in the
		// windowe between when we sampled coremap and before we locked
		// the page table entry.
		spinlock_acquire(&coremap_lock);
		dirty = (coremap[p].status & VM_CORE_DIRTY) || 
		  (old_core.status & VM_CORE_DIRTY);
		spinlock_release(&coremap_lock);
        result = save_page(old_core.pte, dirty);
		if (result) {
			return result;
		}
        old_core.pte->status &= ~VM_PTE_VALID;
        old_core.pte->paddr = (paddr_t)NULL;
	}
	lock_release(old_core.pte->lock);
	kvaddr = PADDR_TO_KVADDR(*paddr);
	bzero((void *)kvaddr, PAGE_SIZE);
	return 0;
}

/*
 * Returns coremap index of contiguous cluster of npages.
 *
 * Caller is responsible for locking coremap.
 * 
 * Args:
 *   npages: Number of contiguous pages to find.
 * 
 * Returns:
 *   Coremap index > 0 if successful, else 0.
 */
static unsigned 
get_ppages(unsigned npages)
{
    unsigned p;  // Page index into coremap.
	unsigned cluster_pages;

    KASSERT(spinlock_do_i_hold(&coremap_lock));
	KASSERT(npages > 0);

	p = next_fit;
	KASSERT(p < page_max);
	cluster_pages = get_core_npages(p);
	while ((coremap[p].status & VM_CORE_USED) || (cluster_pages < npages)) {
		p = (p + cluster_pages) % page_max;
		if (p == next_fit) {
			// No free pages.
			return 0;
		}
		cluster_pages = get_core_npages(p);
	}
	return p;
}

/*
 * Assign coremap page for paddr to addrspace as, vaddr.
 *
 * Caller is responsible for locking coremap.
 * 
 * Args:
 *   paddr: Phyiscal address to assign.
 *   pte: Pointer to corresponding page table entry.
 *   vaddr: Virtual address corresponding to paddr.
 * 
 * Returns:
 *   coremap index of paddr.
 */
unsigned
coremap_assign_vaddr(paddr_t paddr, struct pte *pte, vaddr_t vaddr) {
    unsigned p;

    KASSERT(spinlock_do_i_hold(&coremap_lock));
	p = paddr_to_core_idx(paddr);
	coremap[p].pte = pte;
	coremap[p].vaddr = vaddr;
	return p;
}

/*
 * Assign pages at coremap index p to kernel.
 *
 * Caller is responsible for locking coremap.
 * 
 * Args:
 *   p: coremap index to assign to.
 *   npages: Number of pages to assign.
 * 
 * Returns:
 *   Physical address of assigned cluster.
 */
paddr_t
coremap_assign_to_kernel(unsigned p, unsigned npages)
{
	paddr_t paddr;
	vaddr_t kvaddr;
	unsigned cluster_pages;
	unsigned prev;
	unsigned next;

    KASSERT(spinlock_do_i_hold(&coremap_lock));

	cluster_pages = get_core_npages(p);
	KASSERT(cluster_pages >= npages);
	paddr = core_idx_to_paddr(p);
	KASSERT((paddr & PAGE_FRAME) == paddr);
	KASSERT((paddr >= firstpaddr) && (paddr <= lastpaddr));
	kvaddr = PADDR_TO_KVADDR(paddr);
	coremap[p].status =	set_core_status(/*used=*/1, 0, 0, npages);
	coremap[p].vaddr = kvaddr;
	coremap[p].pte = NULL;

	// Split out remaining free pages if any as a new cluster.
	prev = p;
	p += npages;
	KASSERT(p <= page_max);
	if (p == page_max) {
		p = 0;
	} else if (cluster_pages > npages) {
		coremap[p].status = set_core_status(/*used=*/0, 0, 0, cluster_pages - npages);
		coremap[p].vaddr = (vaddr_t)NULL;
		coremap[p].pte = NULL;
		coremap[p].prev = prev;
		next = prev + cluster_pages;
		if (next < page_max) {
            coremap[next].prev = p;
		}
	}
	next_fit = p;
	
	return paddr;
}

/*
 * Allocates npages of contiguous pages in coremap and assign 
 * to kernel.  Does not modify page table.
 * 
 * Args:
 *   npages: Number of contiguous pages to allocate.
 * 
 * Returns:
 *   Physical address of first page on success, else 0.
 *   We can use physical address of zero as an error condition because
 *   the exception handler is stored there and so we should
 *   never be returning zero (at least on MIPS).
 */
paddr_t 
alloc_pages(unsigned npages)
{
    unsigned p;  // Page index into coremap.
	paddr_t paddr;
	vaddr_t kvaddr;
	int result;
	
	spinlock_acquire(&coremap_lock);
	p = get_ppages(npages);
	if (p == 0) {
		spinlock_release(&coremap_lock);
		// No free pages in coremap.
		if (!swap_enabled || (npages != 1)) {
            // Could not allocate a page.
			return 0;
		}
		result = evict_page(&paddr);
		if (result != 0) {
            // Could not allocate a page.
            return 0;
        }
		return paddr;
	} 
	// Not an eviction, so more memory consumed.
    used_bytes += npages * PAGE_SIZE;
	paddr = coremap_assign_to_kernel(p, npages);
	kvaddr = PADDR_TO_KVADDR(paddr);
	bzero((void *)kvaddr, npages * PAGE_SIZE);
	spinlock_release(&coremap_lock);
	return paddr;
}


static vaddr_t
alloc_kpages_post_boot(unsigned npages)
{
	paddr_t paddr;

	paddr = alloc_pages(npages);
	if (paddr == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(paddr);
}

/*
 * Irreversibly allocate some pages.
 * 
 * Helper function for alloc_kpages_bootstrap.
 */
static paddr_t
getppages_pre_boot(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
	return addr;
}

/* 
 * Allocate/free some kernel-space virtual pages during boot.
 *
 * For any dynamic memory that is allocated at runtime before
 * the virtual memory system is up, this function can
 * be used to allocate pages.  It will never return memory
 * to the system so use sparingly.
 */
static vaddr_t
alloc_kpages_pre_boot(unsigned npages)
{
	paddr_t pa;

	pa = getppages_pre_boot(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

/* 
 * Allocate npages of memory from kernel virtual address space.
 *
 * Wrapper which calls alloc_kpages_func.  This indirection
 * allows us to have a different allocator at boot before
 * the virtual memory system is up for setting up runtime
 * objects.
 * 
 * Args:
 *   npages: Number of contiguous pages to allocate.
 * 
 * Returns:
 *   Virtual address from kernel segment of first page, else NULL
 *   if unsuccessful.
 */

vaddr_t
alloc_kpages(unsigned npages)
{
	if (coremap_enabled) {
		return alloc_kpages_post_boot(npages);
	}
	return alloc_kpages_pre_boot(npages);
}

/*
 * Frees a cluster of kernel pages starting at vaddr.
 */
void
free_kpages(vaddr_t vaddr)
{
	paddr_t paddr;

	if (coremap_enabled) {
        KASSERT((vaddr & PAGE_FRAME) == vaddr);
        paddr = KVADDR_TO_PADDR(vaddr);
        free_pages(paddr);
	}
}

/*
 * Frees cluster of pages starting at paddr from coremap.
 *
 */
void
free_pages(paddr_t paddr)
{
    unsigned p;
	unsigned npages;
	unsigned next;
	unsigned prev;
	vaddr_t vaddr;

	KASSERT((paddr >= firstpaddr) && (paddr < lastpaddr));
	p = paddr_to_core_idx(paddr);

	spinlock_acquire(&coremap_lock);

	// Free this cluster.
	KASSERT(coremap[p].status & VM_CORE_USED);
	npages = get_core_npages(p);
	vaddr = coremap[p].vaddr;
	for (unsigned i = 0; i < npages; i++) {
        vm_tlb_remove(vaddr);
		vaddr += PAGE_SIZE;
	}

	coremap[p].status &= VM_CORE_NPAGES;
	coremap[p].vaddr = (vaddr_t)NULL;
	coremap[p].pte = NULL;
	used_bytes -= npages * PAGE_SIZE;

	// Attempt to coalesce next cluster.
	next = p + npages;
	if ((next < page_max) && !(coremap[next].status & VM_CORE_USED)) {
		npages += get_core_npages(next);
		KASSERT(npages <= VM_CORE_NPAGES);
        coremap[p].status &= ~VM_CORE_NPAGES;
		coremap[p].status |= npages & VM_CORE_NPAGES;
		// If next_fit was on the coalesced cluster, move to new head of cluster.
		if (next_fit == next) {
			next_fit = p;
		}
		next = p + npages;
		if (next < page_max) {
            coremap[next].prev = p;
		}
	}

	// Attempt to coalesce previous cluster.
	prev = coremap[p].prev;
	if (!(coremap[prev].status & VM_CORE_USED)) {
        npages += get_core_npages(prev);
		KASSERT(npages <= VM_CORE_NPAGES);
        coremap[prev].status &= ~VM_CORE_NPAGES;
		coremap[prev].status |= npages & VM_CORE_NPAGES;
		// If next_fit was on the coalesced cluster, move to new head of cluster.
		if (next_fit == p) {
			next_fit = prev;
		}
		if (next < page_max) {
            coremap[next].prev = prev;
		}
	}

	spinlock_release(&coremap_lock);
}

unsigned int
coremap_used_bytes() {
	unsigned result;

	if (coremap_enabled) {
        spinlock_acquire(&coremap_lock);
        result = used_bytes;
        spinlock_release(&coremap_lock);
        return result;
	}
	return 0;
}

/*
 * Handles interprocess interrupt for a TLB shootdown request.
 */
void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
 	vm_tlb_remove(ts->vaddr);
	// Signal back to requester we are done.
	V(ts->sem);
}

/*
 * Flags a page as dirty in coremap and TLB.
 * 
 * Returns:
 *   0 on success, else -1.
 */
static int
flag_page_as_dirty(vaddr_t vaddr)
{
	paddr_t paddr;
	unsigned p;
	int spl;
	int idx;
	uint32_t entryhi, entrylo;

	// Only user space pages should be in the TLB.
	KASSERT(vaddr < MIPS_KSEG0);

	spinlock_acquire(&coremap_lock);
	spl = splhigh();
	idx = tlb_probe(vaddr & PAGE_FRAME, 0);
	if (idx < 0) {
        splx(spl);
        spinlock_release(&coremap_lock);
        return -1;
    }
    tlb_read(&entryhi, &entrylo, idx);
    paddr = entrylo & TLBLO_PPAGE;
    entrylo |= TLBLO_DIRTY;
    tlb_write(entryhi, entrylo, idx);
    p = paddr_to_core_idx(paddr);
    coremap[p].status |= VM_CORE_DIRTY;
    splx(spl);
    spinlock_release(&coremap_lock);

    return 0;
}

/*
 * Inserts a valid page table entry into translation lookaside buffer
 * or replaces existing mapping.
 */
static void
vm_tlb_insert(paddr_t paddr, vaddr_t vaddr)
{
	uint32_t ehi, elo;
	int spl;
	int idx;

	// Only user space pages should be in the TLB.
	KASSERT(vaddr < MIPS_KSEG0);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = vaddr & PAGE_FRAME;
	elo = paddr | TLBLO_VALID;
	KASSERT(elo & TLBLO_VALID);
	DEBUG(DB_VM, "vm_tlb_insert: 0x%x -> 0x%x\n", vaddr, paddr);
	// Check if vaddr already in TLB so we don't duplicate.
	idx = tlb_probe(ehi, 0);
	if (idx < 0) {
        // Don't use tlb_random() because it can't access all NUM_TLB entries.
		idx = random() % NUM_TLB;
	}
	tlb_write(ehi, elo, idx);
	splx(spl);
}

/*
 * Lock coremap and find victim page.
 *
 * Do not use.  For testing only.
 */
paddr_t
locking_find_victim_page()
{
	paddr_t paddr;
	spinlock_acquire(&coremap_lock);
	paddr = find_victim_page();
	spinlock_release(&coremap_lock);
	return paddr;
}

static void
touch_paddr(paddr_t paddr) {
	unsigned p;

	KASSERT(spinlock_do_i_hold(&coremap_lock));
	p = paddr_to_core_idx(paddr);
	coremap[p].status |= VM_CORE_ACCESSED;
}

/*
 * Allocates a new page and either clones from existing page if it is in 
 * memory else restores from swap if not.
 * 
 * Returns:
 *   0 on success else errno.
 */
int
restore_page(struct addrspace *as, struct pte *pte, vaddr_t vaddr)
{
	paddr_t new_paddr;
	int result;

    KASSERT(!lock_do_i_hold(as->pages_lock));
	KASSERT(lock_do_i_hold(pte->lock));

    new_paddr = alloc_pages(1);
    if (new_paddr == 0) {
        return ENOMEM;
    }
    if (pte->status & VM_PTE_VALID) {
		// Clone page in memory pointed to by pte (e.g. copy on write).
        memmove((void *)PADDR_TO_KVADDR(new_paddr),
                (const void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE);
    } else if (pte->status & VM_PTE_BACKED) {
		// Restore from swap.
        KASSERT(swap_enabled);
        result = block_read(pte->block_index, new_paddr);
        if (result) {
            free_pages(new_paddr);
            lock_release(pte->lock);
            return EIO;
        }
    } // Else first access to new page.
    spinlock_acquire(&coremap_lock);
    coremap_assign_vaddr(new_paddr, pte, vaddr);
    pte->paddr = new_paddr;
	touch_paddr(new_paddr);
	pte->status |= VM_PTE_VALID;
	vm_tlb_insert(pte->paddr, vaddr);
    spinlock_release(&coremap_lock);
    return 0;
}

/*
 * Retrieve page containing faultaddress.
 *
 * Search page table for faultaddress.
 * If we find it, just update the TLB and return.
 * If not found, allocate a new page in memory.
 * If page is paged out, then page in from swapdisk.
 * Update page table and TLB.
 * 
 * Args:
 *   as: Pointer to address space.
 *   faultaddress: Page-aligned address that caused a TLB fault.
 * 
 * Returns:
 *   0 on success, else errno value.
 */
int
get_page_via_table(struct addrspace *as, vaddr_t faultaddress)
{
	struct pte *pte;
	int result;

#if OPT_VM_PERF
    count_fault();
#endif

	lock_acquire(as->pages_lock);	
	pte = as_touch_pte(as, faultaddress);
	lock_release(as->pages_lock);
	if (pte == NULL) {
		return ENOMEM;
	}
	// Easy case: page is in memory, just update TLB.
	lock_acquire(pte->lock);
	if (pte->status & VM_PTE_VALID) {
        KASSERT((pte->paddr & PAGE_FRAME) == pte->paddr);
        vm_tlb_insert(pte->paddr, faultaddress);
		spinlock_acquire(&coremap_lock);
		touch_paddr(pte->paddr);
		spinlock_release(&coremap_lock);
		lock_release(pte->lock);
#if OPT_VM_PERF
        count_tlb_fault();
#endif		
        return 0;
	}

	// Harder case: page is not in memory, allocate a new page
	// and restore if it was previously swapped out.
	result = restore_page(as, pte, faultaddress);
	lock_release(pte->lock);
	return result;
}

/*
 * Handles write page fault.  Copy on write if needed, mark dirty.
 *
 * Returns:
 *   0 on success else errno.
 */
int
handle_write_fault(struct addrspace *as, vaddr_t faultaddress)
{
	struct pte *pte;
	struct pte *copy;
	int result;

	lock_acquire(as->pages_lock);
	pte = as_lookup_pte(as, faultaddress);
	lock_release(as->pages_lock);
	KASSERT(pte != NULL);
	lock_acquire(pte->lock);
	if (pte->ref_count > 1) {
        // Copy on write: make a private copy of the page.
		copy = as_create_pte();
		if (copy == NULL) {
			return ENOMEM;
		}
		copy->status = pte->status;
		copy->paddr = pte->paddr;
		copy->block_index = pte->block_index;
		lock_acquire(copy->lock);
        result = restore_page(as, copy, faultaddress);
		lock_release(copy->lock);
        if (result) {
			as_destroy_pte(copy);
			lock_release(pte->lock);
            return result;
        }
		lock_acquire(as->pages_lock);
		// TODO(aabo): combine above page table traversal with this using touch_leaf_pages.
		as_insert_pte(as, faultaddress, copy, NULL);
		lock_release(as->pages_lock);
		pte->ref_count--;
    }
    lock_release(pte->lock);
	return flag_page_as_dirty(faultaddress);
}

/*
 * Handles translation lookaside buffer faults.
 *
 * If faultaddress is in a valid segment for this address space
 * then:
 * 
 * If page is resident in coremap, proceed.
 * If page has never been accessed, allocate a page.
 * If page is swapped out then page in.
 * 
 * Finally, update TLB with page and return for retry.
 *
 * Args:
 *   faulttype: enumerated fault type defined in vm.h.
 *   faultaddress: access to address that caused fault.
 * 
 * Returns:
 *   0 on success, else errno.
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as;
	int read_request;
	int result;

	// WARNING: Using kprintf in this function may cause TLB to
	// behave unexpectedly.

	// TLB faults should only occur in KUSEG.
	// TODO(aabo): change to KASSERT.
	if (faultaddress >= MIPS_KSEG0) {
		panic("vm_fault: faultaddress = 0x%08x is not in KUSEG.\n", 
		  faultaddress);
	}
	DEBUG(DB_VM, "vm_fault: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space is not empty. */
	KASSERT(as->next_segment != 0);

	read_request = faulttype == VM_FAULT_READ;
	if (!as_operation_is_valid(as, faultaddress, read_request)) {
		return EFAULT;
	}
	faultaddress &= PAGE_FRAME;
	// All TLB entries are initially read-only (TLB "dirty=0").
	// We detect valid writes as VM_FAULT_READONLY (not a permission fault).
	if (faulttype == VM_FAULT_READONLY) {
        result = handle_write_fault(as, faultaddress);
		if (result >= 0) {			
			return result;
		}
		// Else page is no longer in TLB, so treat as a read fault.
		// We will need to update TLB and re-fault on write.
	}
	result = get_page_via_table(as, faultaddress);
	if (result) {
		return result;
	}
	return 0;
}
