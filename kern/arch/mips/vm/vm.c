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
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

static struct spinlock coremap_lock;

// Acquire coremap_lock before accessing any of these shared variables.
static paddr_t firstpaddr;  // First byte that can be allocated.
static paddr_t lastpaddr;   // Last byte that can be allocated.
static struct core_page *coremap;
static unsigned used_bytes;
static unsigned page_max;  // Total number of allocatable pages.
static unsigned next_fit;  // Coremap index to resume free page search.

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

	for (p = 0; p < page_max; p += npages) {
		npages = get_core_npages(p);
		status = coremap[p].status;
		kprintf("coremap[%3u]: status=0x%08x, vaddr=0x%08x, npages=%u\n", p, status, 
		  coremap[p].vaddr, npages);
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
static paddr_t
core_idx_to_paddr(unsigned p)
{
    return (paddr_t)(p * PAGE_SIZE);
}

/* 
 * Converts physical address to coremap index.
 */
static unsigned
paddr_to_core_idx(paddr_t paddr)
{
    return (unsigned)(paddr / PAGE_SIZE);
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

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = vaddr;
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
 */
static void
validate_coremap()
{
	unsigned p;
	unsigned used_pages = 0;
	unsigned free_pages = 0;
	unsigned npages;
    unsigned status;

	for (p = 0; p < page_max;) {
		npages = get_core_npages(p);
		status = coremap[p].status;
        if (status & VM_CORE_USED) {
			used_pages += npages;
		} else {
			free_pages += npages;
		}
		p += npages;
	}
	if (used_pages * PAGE_SIZE != used_bytes) {
		kprintf("used_pages = %u\n", used_pages);
		kprintf("used_bytes = %u\n", used_bytes);
		dump_coremap();
		panic("used_pages * PAGE_SIZE (%u) != used_bytes (%u)",
		  used_pages * PAGE_SIZE, used_bytes);
	}
	if (used_pages + free_pages != page_max) {
		kprintf("used_pages = %u\n", used_pages);
		kprintf("free_pages = %u\n", free_pages);
		kprintf("page_max = %u\n", page_max);
		dump_coremap();
		panic("used_pages + free_pages (%u) != page_max (%u)",
		  used_pages + free_pages, page_max);
	}
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
	// convert to a virtual address and zero out.
	coremap = (struct core_page *)PADDR_TO_KVADDR(coremap_paddr);
	bzero((void *)coremap, coremap_bytes);	
	// Mark kernel and coremap pages as allocated in coremap.
	p = paddr_to_core_idx(firstpaddr);
	coremap[0].status = set_core_status(1, 0, 0, p);
	// Mark remainder of pages as free.
	coremap[p].status = set_core_status(0, 0, 0, page_max - p);
	next_fit = p;
	// Includes kernel and coremap in used_bytes.
	used_bytes = p * PAGE_SIZE;
	spinlock_init(&coremap_lock);
	validate_coremap();

	kprintf("\nvm_init_coremap\n");
	kprintf("lastpaddr  = 0x%07x\n", lastpaddr);
	kprintf("firstpaddr = 0x%07x\n", firstpaddr);
	kprintf("coremap    = 0x%07x\n", coremap_paddr);
	kprintf("page_max   = %u\n", page_max);
	kprintf("\n");
}

void
vm_bootstrap(void)
{
	/* Do nothing. */
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
 * Allocates npages of contiguous pages in coremap and assign 
 * to addrspace as at virtual address vaddr.
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
 */
paddr_t alloc_pages(unsigned npages, struct addrspace *as, vaddr_t vaddr)
{
	paddr_t paddr;
	vaddr_t vaddr_direct;
    unsigned p;  // Page index into coremap.
	unsigned block_pages;

	spinlock_acquire(&coremap_lock);
	p = next_fit;
	KASSERT(p < page_max);
	block_pages = get_core_npages(p);
	while ((coremap[p].status & VM_CORE_USED) || (block_pages < npages)) {
		p = (p + block_pages) % page_max;
		if (p == next_fit) {
			// No free pages.
            spinlock_release(&coremap_lock);
            return 0;
		}
		block_pages = get_core_npages(p);
	}
	paddr = core_idx_to_paddr(p);
	KASSERT((paddr & PAGE_FRAME) == paddr);
	KASSERT(paddr >= firstpaddr);  // Don't overwrite kernel.
	vaddr_direct = PADDR_TO_KVADDR(paddr);
	bzero((void *)vaddr_direct, npages * PAGE_SIZE);
	coremap[p].status = set_core_status(1, 0, 0, npages);
	coremap[p].as = as;
	if ((as == NULL) || (vaddr == (vaddr_t)NULL)) {
		// Kernel page.
        coremap[p].vaddr = vaddr_direct;
	} else {
		// Userspace page.
		// We only allocate user pages one at a time.
		// We don't support freeing contiguous groups of user pages.
		KASSERT(npages == 1);
		coremap[p].vaddr = vaddr;
	}
	p = (p + npages) % page_max;
	if (block_pages > npages) {
		// Split out remaining block of pages.
		coremap[p].status = set_core_status(0, 0, 0, block_pages - npages);
	}
	next_fit = p;
	used_bytes += npages * PAGE_SIZE;
	validate_coremap();
	spinlock_release(&coremap_lock);
	return paddr;
}

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
	vaddr_t vaddr;

	KASSERT((paddr > firstpaddr) && (paddr < lastpaddr));
	p = paddr_to_core_idx(paddr);
	spinlock_acquire(&coremap_lock);
	KASSERT(coremap[p].status & VM_CORE_USED);
	npages = get_core_npages(p);
	vaddr = coremap[p].vaddr;
	for (unsigned i = 0; i < npages; i++) {
        vm_tlb_remove(vaddr);
		vaddr += PAGE_SIZE;
	}
	coremap[p].status &= ~VM_CORE_USED;
	used_bytes -= npages * PAGE_SIZE;
	// Attempt to coalesce next block.
	// Does not work well if pages are freed in ascending order.
	next = p + npages;
	if ((next < page_max) && !(coremap[next].status & VM_CORE_USED)) {
		npages += get_core_npages(next);
		KASSERT(npages <= VM_CORE_NPAGES);
        coremap[p].status &= ~VM_CORE_NPAGES;
		coremap[p].status |= npages & VM_CORE_NPAGES;
		// If next_fit was on the coalesced block, move to new head of block.
		if (next_fit == next) {
			next_fit = p;
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

	spinlock_acquire(&coremap_lock);
	spl = splhigh();
	tlb_idx = tlb_probe(vaddr, 0);
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
static void
vm_tlb_dump()
{
	int spl;
	uint32_t entryhi, entrylo;
	vaddr_t vaddr;
	paddr_t paddr;
	int dirty;
	int valid;

	spl = splhigh();
	kprintf("vm_tlb_dump:\n");
	for (int idx = 0; idx < NUM_TLB; idx++) {
		tlb_read(&entryhi, &entrylo, idx);
		vaddr = entryhi & TLBHI_VPAGE;
		paddr = entrylo & TLBLO_PPAGE;
		dirty = entrylo & TLBLO_DIRTY ? 1 : 0;
		valid = entrylo & TLBLO_VALID ? 1 : 0;
		if (valid) {
		  kprintf("[%2d] v0x%08x -> p0x%08x, dirty=%d, valid=%d, 0x%x\n", idx, 
		    vaddr, paddr, dirty, valid, entrylo);
		}
	}
	splx(spl);
}
*/

/*
 * Inserts a valid page table entry into translation lookaside buffer.
 */
static void
vm_tlb_insert(paddr_t paddr, vaddr_t vaddr)
{
	uint32_t ehi, elo;
	int spl;
	int idx;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = vaddr;
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

/*
 * Handles translation lookaside buffer faults.
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
	struct pte *pte;

	// WARNING: Using kprintf in this function may cause TLB to
	// behave unexpectedly.

	// TLB faults should only occur in KUSEG.
	KASSERT(faultaddress <= MIPS_KSEG0);
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
	// We detect writes as VM_FAULT_READONLY and flag as dirty
	// for page cleaning.  Set write enable (TLB "dirty=1")
	// and retry.
	if (faulttype == VM_FAULT_READONLY) {
		if (flag_page_as_dirty(faultaddress) == 0) {
			// Successfully flagged, retry access.
            return 0;
		}
		// Page is no longer in TLB.
	}
	// Find or create a page table entry.
	lock_acquire(as->pages_lock);
	pte = as_touch_pte(as, faultaddress);
	if (pte == NULL) {
		lock_release(as->pages_lock);
		return ENOMEM;
	}
	if (!(pte->status & VM_PTE_VALID)) {
		// TODO(aabo): Insert swapping logic.
		// First access such as heap, stack, load_segment, 
		// so allocate a new page.
		pte->paddr = alloc_pages(1, as, faultaddress);
		if (pte->paddr == (paddr_t)NULL) {
			lock_release(as->pages_lock);
			return ENOMEM;
		}
		pte->status |= VM_PTE_VALID;
	}
	/* make sure it's page-aligned */
	KASSERT((pte->paddr & PAGE_FRAME) == pte->paddr);
	vm_tlb_insert(pte->paddr, faultaddress);
	lock_release(as->pages_lock);
	return 0;
}
