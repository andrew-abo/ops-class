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
		panic("used_pages * PAGE_SIZE != used_bytes");
	}
	KASSERT(used_pages * PAGE_SIZE == used_bytes);
	if (used_pages + free_pages != page_max) {
		kprintf("used_pages = %u\n", used_pages);
		kprintf("free_pages = %u\n", free_pages);
		kprintf("page_max = %u\n", page_max);
		panic("used_pages + free_pages != page_max");
	}
	KASSERT(used_pages + free_pages == page_max);
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
	size_t raw_bytes;
	size_t avail_bytes;
	size_t coremap_bytes;
	unsigned int raw_pages;
	paddr_t firstfree;
	paddr_t coremap_paddr;

	lastpaddr = ram_getsize();

	// First available physical address just above kernel image 
	// and first thread stack.
	firstfree = ram_getfirstfree();
	used_bytes = 0;

	// Total memory in bytes minus the kernel code.
	raw_bytes = lastpaddr - firstfree;
	raw_pages = raw_bytes / PAGE_SIZE;
	coremap_bytes = raw_pages * sizeof(struct core_page);
	avail_bytes = raw_bytes - coremap_bytes;
	page_max = avail_bytes / PAGE_SIZE;

	// &coremap[0] aligned up to core_page size.	
	coremap_paddr = (firstfree + sizeof(struct core_page) - 1) & 
	    ~(sizeof(struct core_page) - 1);

	// First allocatable page is above coremap and page aligned up.
	firstpaddr = coremap_paddr + coremap_bytes;
	firstpaddr = (firstpaddr + PAGE_SIZE - 1) & PAGE_FRAME;

	kprintf("\nvm_init_coremap\n");
	kprintf("lastpaddr  = 0x%07x\n", lastpaddr);
	kprintf("firstpaddr = 0x%07x\n", firstpaddr);
	kprintf("coremap    = 0x%07x\n", coremap_paddr);
	kprintf("coremap_bytes = %u\n", coremap_bytes);
	kprintf("last - first = %u\n", lastpaddr - firstpaddr);
	kprintf("page_max = %u\n", page_max);
	kprintf("\n");

	// From this point, we will be accessing coremap directly, so
	// convert to a virtual address and clear the coremap.
	coremap = (struct core_page *)PADDR_TO_KVADDR(coremap_paddr);
	bzero((void *)coremap, coremap_bytes);
	coremap[0].status = set_core_status(0, 0, 0, page_max);
	next_fit = 0;
	spinlock_init(&coremap_lock);
	validate_coremap();
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
	vaddr_t vaddr;
    unsigned p;  // Page index into coremap.
	unsigned block_pages;

	spinlock_acquire(&coremap_lock);
	p = next_fit;
	KASSERT(p < page_max);
	//kprintf("alloc_kpages(%u)\n", npages);
	block_pages = get_core_npages(p);
	while ((coremap[p].status & VM_CORE_USED) || (block_pages < npages)) {
		//kprintf("scanning p = %u\n", p);
		p = (p + block_pages) % page_max;
		if (p == next_fit) {
            spinlock_release(&coremap_lock);
            panic("out of memory");
            return 0;
		}
		block_pages = get_core_npages(p);
	}
	paddr = firstpaddr + p * PAGE_SIZE;
	KASSERT((paddr & PAGE_FRAME) == paddr);
	KASSERT(paddr >= firstpaddr);  // Don't overwrite kernel.
	vaddr = PADDR_TO_KVADDR(paddr);
	// TODO(aabo): Defer zero filling until pages are accessed in fault handler.
	bzero((void *)vaddr, npages * PAGE_SIZE);
	coremap[p].vaddr = vaddr;
	coremap[p].status = set_core_status(1, 0, 0, npages);
	coremap[p].as = NULL;
	p = (p + npages) % page_max;
	if (block_pages > npages) {
		// Split out remaining block of pages.
		coremap[p].status = set_core_status(0, 0, 0, block_pages - npages);
	}
	next_fit = p;
	used_bytes += npages * PAGE_SIZE;
	validate_coremap();
	spinlock_release(&coremap_lock);
	kprintf("alloc_kpages() -> 0x%x\n", vaddr);
	return vaddr;
}

void
free_kpages(vaddr_t addr)
{
    unsigned page_idx;

    KASSERT((addr & PAGE_FRAME) == addr);
	page_idx = KVADDR_TO_PADDR(addr) >> PAGE_SIZE_MSB;
	spinlock_acquire(&coremap_lock);
	kprintf("free_kpages(0x%x) 0x%x\n", addr, coremap[page_idx].status);
	// TODO(aabo): This assertion fails on km2.
	KASSERT(coremap[page_idx].status & VM_CORE_USED);
	coremap[page_idx].status &= ~VM_CORE_USED;
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

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

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

	/* Assert that the address space has been set up properly. */
	//KASSERT(as->as_vbase1 != 0);

	// Check address is in valid defined region.
	//vbase1 = as->as_vbase1;
	//vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	//stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	//stacktop = USERSTACK;

	// If invalid return EFAULT.
	paddr = 0;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
