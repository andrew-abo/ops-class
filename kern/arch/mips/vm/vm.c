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

// Dummy value to help detect kernel stack overflow.
#define STACK_CANARY 0xbaddcafe

static struct spinlock coremap_lock;

// Acquire coremap_lock before accessing any of these shared variables.
static paddr_t firstpaddr;  // First byte that can be allocated.
static paddr_t lastpaddr;   // Last byte that can be allocated.
static struct core_page *coremap;
static uint32_t *kernel_stack_bottom;  // Pointer to bottom of kernel stack.
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
		kprintf("coremap[%3u]: status=0x%08x, paddr=0x%08x, vaddr=0x%08x, npages=%u\n", 
		  p, status, core_idx_to_paddr(p), coremap[p].vaddr, npages);
	}
}

/*
 * Returns 1 if kernel stack canary is still intact (appears not corrupted),
 * else 0.
 */
int
kernel_stack_ok()
{
    return *kernel_stack_bottom == STACK_CANARY ? 1 : 0;
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
 * Returns addrspace that paddr belongs to.
 */
struct addrspace 
*vm_get_as(paddr_t paddr)
{
    unsigned p;
	struct addrspace *as;

	p = paddr_to_core_idx(paddr);
	spinlock_acquire(&coremap_lock);
	as = coremap[p].as;
	spinlock_release(&coremap_lock);
	return as;
}

/*
 * Returns virtual address mapped to paddr.
 */
vaddr_t
vm_get_vaddr(paddr_t paddr)
{
    unsigned p;
    vaddr_t vaddr;

	p = paddr_to_core_idx(paddr);
	spinlock_acquire(&coremap_lock);
	vaddr = coremap[p].vaddr;
	spinlock_release(&coremap_lock);
	return vaddr;
}

/*
 * Invalidates one entry in TLB.
 *
 */
static void 
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
 * Debug check to confirm coremap is valid.
 * Caller is responsible for locking coremap.
 * 
 * Returns:
 *   0 if coremap is valid, else panics.
 */
static int
validate_coremap()
{
	unsigned p;
	unsigned used_pages = 0;
	unsigned free_pages = 0;
	unsigned npages;
    unsigned status;

    KASSERT(spinlock_do_i_hold(&coremap_lock));
    KASSERT(next_fit < page_max);
	for (p = 0; p < page_max;) {
		if (p > 0) {
			KASSERT(coremap[p].prev == p - npages);
		}
		npages = get_core_npages(p);
		status = coremap[p].status;
        if (status & VM_CORE_USED) {
			used_pages += npages;
			if (coremap[p].as == NULL) {
				// Kernel page.
                KASSERT(coremap[p].vaddr >= MIPS_KSEG0);
                KASSERT(coremap[p].vaddr < MIPS_KSEG1);
            } else {
				// User page.
                KASSERT(coremap[p].vaddr < MIPS_KSEG0);
            }
		} else {
			free_pages += npages;
		}
		p += npages;
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
	KASSERT(*kernel_stack_bottom == STACK_CANARY);
	return 0;
}

/*
 * Initializes the physical to virtual memory map.
 *
 * Must be run after ram_bootstrap() and before kmalloc().
 * will function.
 */
void
vm_init_coremap()
{
	size_t total_bytes;  // total bytes in physical memory.
	size_t coremap_bytes;
	paddr_t kernel_top;  // Immediately above kernel code.
	paddr_t coremap_paddr;
	unsigned p;

	lastpaddr = ram_getsize();
	kernel_top = ram_getfirstfree();

	// Total memory in bytes minus the kernel code.
	total_bytes = lastpaddr;
	page_max = paddr_to_core_idx(total_bytes);
	coremap_bytes = page_max * sizeof(struct core_page);

	// &coremap[0] aligned up to core_page size.	
	coremap_paddr = (kernel_top + sizeof(struct core_page) - 1) & 
	    ~(sizeof(struct core_page) - 1);

	// First allocatable page is above coremap and page aligned up.
	firstpaddr = coremap_paddr + coremap_bytes;
	firstpaddr = (firstpaddr + PAGE_SIZE - 1) & PAGE_FRAME;

	// From this point, we will be accessing coremap directly, so
	// convert to a direct mapped virtual address and zero out.
	coremap = (struct core_page *)PADDR_TO_KVADDR(coremap_paddr);
	// Place canary value to help detect kernel stack overflow.
	kernel_stack_bottom = (uint32_t *)((char *)coremap - PAGE_SIZE);
	*kernel_stack_bottom = STACK_CANARY;
	bzero((void *)coremap, coremap_bytes);	
	// Mark kernel and coremap pages as allocated in coremap.
	p = paddr_to_core_idx(firstpaddr);
	coremap[0].status = set_core_status(1, 0, 0, p);
	coremap[0].as = NULL;
	coremap[0].vaddr = (vaddr_t)MIPS_KSEG0;
	coremap[0].prev = 0;
	// Mark remainder of pages as one big free block.
	KASSERT(p < page_max);
	coremap[p].status = set_core_status(0, 0, 0, page_max - p);
	coremap[p].prev = 0;
	next_fit = p;
	// Includes kernel and coremap in used_bytes.
	used_bytes = p * PAGE_SIZE;
	spinlock_init(&coremap_lock);
	KASSERT(validate_coremap() == 0);

	kprintf("\nvm_init_coremap\n");
	kprintf("lastpaddr  = 0x%08x\n", lastpaddr);
	kprintf("firstpaddr = 0x%08x\n", firstpaddr);
	kprintf("coremap    = 0x%08x\n", coremap_paddr);
	kprintf("page_max   = %u\n", page_max);
	kprintf("*kernel_stack_bottom = 0x%08x\n", *(uint32_t *)kernel_stack_bottom);
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
        swap_enabled = 0;
        return;
	}
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
 *   coremap index of freed page, else 0 if no pages available.
 */
static unsigned
find_victim_page()
{
	unsigned p;
	unsigned npages;
	unsigned victim;
	unsigned user_pages = 0;
	
	KASSERT(spinlock_do_i_hold(&coremap_lock));

	// TODO(aabo): Replace simple policy with higher performance policy
	// such as CLOCK.

	// Inventory available user pages.
	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		// First choice are pages that became free since we last checked.
		if (!(coremap[p].status & VM_CORE_USED)) {
			return p;
		}
		if (coremap[p].as == NULL) {
			// Skip kernel pages.
			continue;
		}
		user_pages++;
	}
	if (user_pages == 0) {
        return 0;
	}
	// Choose one of the occupied user pages at random.
	victim = random() % user_pages;
	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		if (coremap[p].vaddr >= MIPS_KSEG0) {
			// Skip kernel pages.
			continue;
		}
		if (victim == 0) {
			return p;
		}
		victim--;
	}
	panic("find_victim_page: failed to find victim page.");
}

/*
 * Swap out a page if needed.
 *
 * Args:
 *   pte: Pointer to page table entry of page to maybe swap out.
 *   dirty: 1 if page has been modified, else 0.
 * 
 * Returns:
 *   0 on success else errno.
 */
int
maybe_swap_out(struct pte *pte, int dirty) {
	unsigned block_index;
	int result;

	// Swap page out if needed.
	KASSERT(pte != NULL);
	if (!(pte->status & VM_PTE_BACKED)) {
        lock_acquire(swapmap_lock);
        result = bitmap_alloc(swapmap, &block_index);
		if (result) {
			return result;
		}
        lock_release(swapmap_lock);
	} else {
		block_index = pte->block_index;
	}
	if (dirty || !(pte->status & VM_PTE_BACKED)) {
        result = block_write(block_index, pte->paddr);
        if (result) {
            return ENOSPC;
        }
        pte->block_index = block_index;
        pte->status |= VM_PTE_BACKED;
	}
	pte->status &= ~VM_PTE_VALID;
	pte->paddr = (paddr_t)NULL;
	return 0;
}

/*
 * Finds and evicts a userspace page.
 *
 * Args:
 *   coremap_index: pointer to return coremap index of freed page.
 *
 * Returns:
 *   0 on success, else errno value.
 */
int
evict_page(unsigned *coremap_index)
{
	vaddr_t old_vaddr;
	struct addrspace *old_as;
	struct pte *old_pte;
	int old_status;
	int p;

	// Identify a page to evict.
	spinlock_acquire(&coremap_lock);
	p = find_victim_page();
	if (p == 0) {
		spinlock_release(&coremap_lock);
		return ENOMEM;
	}
	// We assume we are evicting exactly one page.
	KASSERT(get_core_npages(p) == 1);
	old_vaddr = coremap[p].vaddr;
	old_as = coremap[p].as;
	old_status = coremap[p].status;
	// Claim this page for the kernel so on one else evicts it.
	coremap[p].as = NULL;
	coremap[p].vaddr = (vaddr_t)NULL;
	coremap[p].status &= ~(VM_CORE_DIRTY | VM_CORE_ACCESSED);
	coremap[p].status |= VM_CORE_USED;
	spinlock_release(&coremap_lock);

	KASSERT(old_as != NULL);

	if (old_status & VM_CORE_USED) {
        lock_acquire(old_as->pages_lock);
        // Deactivate page so it is not accessed during page out.
        // TODO(aabo): call tlb_shootdown for multi-CPU case.
        vm_tlb_remove(old_vaddr);
        old_pte = as_lookup_pte(old_as, old_vaddr);
        maybe_swap_out(old_pte, old_status & VM_CORE_DIRTY);
        lock_release(old_as->pages_lock);
	}
	*coremap_index = p;
	return 0;
}

/*
 * Returns coremap index of contiguous block of npages.
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
	unsigned block_pages;

    KASSERT(spinlock_do_i_hold(&coremap_lock));
	KASSERT(npages > 0);

	p = next_fit;
	KASSERT(p < page_max);
	block_pages = get_core_npages(p);
	while ((coremap[p].status & VM_CORE_USED) || (block_pages < npages)) {
		p = (p + block_pages) % page_max;
		if (p == next_fit) {
			// No free pages.
			return 0;
		}
		block_pages = get_core_npages(p);
	}
	return p;
}

/*
 * Assign pages at coremap index p to addrspace as @ vaddr.
 *
 * Caller is responsible for locking coremap.
 * 
 * Args:
 *   p: coremap index to assign to.
 *   npages: Number of pages to assign.
 *   as: Address space that will own the page.
 *   vaddr: Virtual address of the page.
 * 
 * Returns:
 *   Physical address of assigned block.
 */
static paddr_t
coremap_assign_pages(unsigned p, unsigned npages, struct addrspace *as, vaddr_t vaddr)
{
	paddr_t paddr;
	vaddr_t vaddr_direct;
	unsigned block_pages;
	unsigned prev;
	unsigned next;

    KASSERT(spinlock_do_i_hold(&coremap_lock));
	KASSERT((as == NULL) || ((as != NULL) && (vaddr < MIPS_KSEG0)));

	block_pages = get_core_npages(p);
	KASSERT(block_pages >= npages);
	paddr = core_idx_to_paddr(p);
	KASSERT((paddr & PAGE_FRAME) == paddr);
	KASSERT((paddr >= firstpaddr) && (paddr <= lastpaddr));
	vaddr_direct = PADDR_TO_KVADDR(paddr);
	bzero((void *)vaddr_direct, npages * PAGE_SIZE);
	used_bytes += npages * PAGE_SIZE;
	coremap[p].status =	set_core_status(/*used=*/1, 0, 0, npages);
	coremap[p].as = as;
	if (as == NULL) {
		// Kernel pages don't have an addrsapce.
		// Pages are direct mapped w/o TLB.
        coremap[p].vaddr = vaddr_direct;
	} else {
		// Userspace page.
		// We only allocate user pages one at a time.
		// We don't support freeing contiguous groups of user pages.
		KASSERT(npages == 1);
		coremap[p].vaddr = vaddr;
	}

	// Split out remaining free pages if any as a new block.
	prev = p;
	p += npages;
	KASSERT(p <= page_max);
	if (p == page_max) {
		p = 0;
	} else if (block_pages > npages) {
		coremap[p].status = set_core_status(/*used=*/0, 0, 0, block_pages - npages);
		coremap[p].as = NULL;
		coremap[p].vaddr = (vaddr_t)NULL;
		coremap[p].prev = prev;
		next = prev + block_pages;
		if (next < page_max) {
            coremap[next].prev = p;
		}
	}
	next_fit = p;
	
	return paddr;
}

/*
 * Allocates npages of contiguous pages in coremap and assign 
 * to addrspace as at virtual address vaddr.  Does not
 * modify page table.
 * 
 * To allocate a kernel page:
 * paddr = alloc_pages(1, NULL, NULL)
 * 
 * Args:
 *   npages: Number of contiguous pages to allocate.
 *   as: Pointer to address space to assign to.
 *   vaddr: Virtual address to assign to first page.
 *          If as==NULL or vaddr==NULL, then vaddr will be
 *          direct mapped to KSEG0.
 * 
 * Returns:
 *   Physical address of first page on success, else 0.
 *   We can use physical address of zero as an error condition because
 *   the exception handler is stored there and so we should
 *   never be returning zero (at least on MIPS).
 */
paddr_t 
alloc_pages(unsigned npages, struct addrspace *as, vaddr_t vaddr)
{
	paddr_t paddr = 0;
    unsigned p;  // Page index into coremap.
	int result;

    spinlock_acquire(&coremap_lock);
	p = get_ppages(npages);
	if (p == 0) {
        spinlock_release(&coremap_lock);
        if (swap_enabled) {
            result = evict_page(&p);
            if (result != 0) {
                // Could not allocate a page.
                return 0;
            }
            spinlock_acquire(&coremap_lock);
		} else {
			// Could not allocate a page.
			return 0;
		}
	}
	paddr = coremap_assign_pages(p, npages, as, vaddr);	
	spinlock_release(&coremap_lock);
	return paddr;
}

/* 
 * Allocate npages of memory from kernel virtual address space.
 *
 * Uses coremap as implicit free list and allocates using a next fit
 * policy.  Fit search is resumed from next_fit index each call and
 * rolls over at page_max.
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
	paddr_t paddr;

	paddr = alloc_pages(npages, NULL, (vaddr_t)NULL);
	if (paddr == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(paddr);
}

/*
 * Fill a block with 0xcafebadd.
 *
 * Assists with dangling pointer detection (aka poisoning).
 */
/*
static
void
fill_deadbeef(void *vptr, size_t len)
{
	uint32_t *ptr = vptr;
	size_t i;

	for (i=0; i<len/sizeof(uint32_t); i++) {
		ptr[i] = 0xdeadbeef;
	}
}
*/

/*
 * Frees a block of kernel pages starting at vaddr.
 */
void
free_kpages(vaddr_t vaddr)
{
	paddr_t paddr;

    KASSERT((vaddr & PAGE_FRAME) == vaddr);
	paddr = KVADDR_TO_PADDR(vaddr);
	free_pages(paddr);
}

/*
 * Frees block of pages starting at paddr from coremap.
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

	// Free this block.
	KASSERT(coremap[p].status & VM_CORE_USED);
	npages = get_core_npages(p);
	vaddr = coremap[p].vaddr;
	for (unsigned i = 0; i < npages; i++) {
        vm_tlb_remove(vaddr);
		vaddr += PAGE_SIZE;
	}
	coremap[p].status &= ~VM_CORE_USED;
	coremap[p].vaddr = (vaddr_t)NULL;
	coremap[p].as = NULL;
	used_bytes -= npages * PAGE_SIZE;

	// Attempt to coalesce next block.
	next = p + npages;
	if ((next < page_max) && !(coremap[next].status & VM_CORE_USED)) {
		npages += get_core_npages(next);
		KASSERT(npages <= VM_CORE_NPAGES);
        coremap[p].status &= ~VM_CORE_NPAGES;
		coremap[p].status |= npages & VM_CORE_NPAGES;
		coremap[p].vaddr = (vaddr_t)NULL;
		coremap[p].as = NULL;
		// If next_fit was on the coalesced block, move to new head of block.
		if (next_fit == next) {
			next_fit = p;
		}
		next = p + npages;
		if (next < page_max) {
            coremap[next].prev = p;
		}
	}

	// Attempt to coalesce previous block.
	prev = coremap[p].prev;
	if (!(coremap[prev].status & VM_CORE_USED)) {
        npages += get_core_npages(prev);
		KASSERT(npages <= VM_CORE_NPAGES);
        coremap[prev].status &= ~VM_CORE_NPAGES;
		coremap[prev].status |= npages & VM_CORE_NPAGES;
		// If next_fit was on the coalesced block, move to new head of block.
		if (next_fit == p) {
			next_fit = prev;
		}
		if (next < page_max) {
            coremap[next].prev = prev;
		}
	}

	spinlock_release(&coremap_lock);
}

unsigned
int
coremap_used_bytes() {
	unsigned result;

	spinlock_acquire(&coremap_lock);
	result = used_bytes;
	spinlock_release(&coremap_lock);
	return result;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm_tlbshootdown not implemented.\n");
}

/*
 * Flags a page as dirty in coremap and TLB.
 * 
 * Returns:
 *   0 on success, else 1.
 */
static int
flag_page_as_dirty(vaddr_t vaddr)
{
	paddr_t paddr;
	unsigned p;
	int spl;
	int tlb_idx;
	uint32_t entryhi, entrylo;

	// Only user space pages should be in the TLB.
	KASSERT(vaddr < MIPS_KSEG0);

	spinlock_acquire(&coremap_lock);
	spl = splhigh();
	tlb_idx = tlb_probe(vaddr & PAGE_FRAME, 0);
	if (tlb_idx < 0) {
        splx(spl);
        spinlock_release(&coremap_lock);
        return 1;
    }
    tlb_read(&entryhi, &entrylo, tlb_idx);
    paddr = entrylo & TLBLO_PPAGE;
    entrylo |= TLBLO_DIRTY;
    tlb_write(entryhi, entrylo, tlb_idx);
    p = paddr_to_core_idx(paddr);
    coremap[p].status |= VM_CORE_DIRTY;
    splx(spl);
    spinlock_release(&coremap_lock);

    return 0;
}

/*
 * Inserts a valid page table entry into translation lookaside buffer.
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
	elo = (paddr) | TLBLO_VALID;
	KASSERT(elo & TLBLO_VALID);
	DEBUG(DB_VM, "vm_tlb_insert: 0x%x -> 0x%x\n", vaddr, paddr);
	// Check if vaddr already in TLB so we don't duplicate.
	idx = tlb_probe(ehi, 0);
	if (idx < 0) {
        tlb_random(ehi, elo);
	} else {
		tlb_write(ehi, elo, idx);
	}
	splx(spl);
}

paddr_t
locking_find_victim_page()
{
	paddr_t paddr;
	spinlock_acquire(&coremap_lock);
	paddr = find_victim_page();
	spinlock_release(&coremap_lock);
	return paddr;
}

/*
 * Retrieve page containing faultaddress.
 *
 * Allocate a new page in memory.
 * If page is paged out, then page in from swapdisk.
 * Update page table.
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

	lock_acquire(as->pages_lock);	
	pte = as_touch_pte(as, faultaddress);
	if (pte == NULL) {
		lock_release(as->pages_lock);
		return ENOMEM;
	}
	if (!(pte->status & VM_PTE_VALID)) {
        // We must release our page table before requesting memory.
		// Otherwise eviction can deadlock if there is a mutual
		// eviction from the victim address space.
		lock_release(as->pages_lock);
        pte->paddr = alloc_pages(1, as, faultaddress);
        lock_acquire(as->pages_lock);
		if (pte->status & VM_PTE_BACKED) {
            KASSERT(swap_enabled);
            result = block_read(pte->block_index, pte->paddr);
            if (result) {
                return EIO;
            }
        }		
    }
	KASSERT((pte->paddr & PAGE_FRAME) == pte->paddr);
	pte->status |= VM_PTE_VALID;
	vm_tlb_insert(pte->paddr, faultaddress);
	lock_release(as->pages_lock);
	return 0;
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
	//KASSERT(faultaddress < MIPS_KSEG0);
	if (faultaddress >= MIPS_KSEG0) {
		panic("faultaddress = 0x%08x >= MIPS_KSEG0", faultaddress);
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
		panic("vm_fault: curproc == NULL");
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		panic("vm_fault: as == NULL");
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
	// We detect writes as VM_FAULT_READONLY and flag as dirty
	// for page cleaning.  Set write enable (TLB "dirty=1")
	// and retry.
	if (faulttype == VM_FAULT_READONLY) {
		if (flag_page_as_dirty(faultaddress) == 0) {
			// Successfully flagged in TLB, retry access.
            return 0;
		}
		// Page is no longer in TLB, so treat as vanilla write page fault.
	}
	result = get_page_via_table(as, faultaddress);
	if (result) {
		return result;
	}
	return 0;
}
