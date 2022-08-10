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
static paddr_t firstpaddr;  // First byte that can be allocated.
static paddr_t lastpaddr;   // Last byte that can be allocated.
static struct core_page *coremap;
static unsigned used_bytes;
static unsigned page_max;

/*
static unsigned core_npages(struct core_page page)
{
    return (page.status) & VM_CORE_NPAGES;
}

static unsigned set_core_status(int used, int accessed, int dirty, unsigned npages)
{
    return (used ? VM_CORE_USED : 0) | 
           (accessed ? VM_CORE_ACCESSED : 0) | 
           (dirty ? VM_CORE_DIRTY : 0) |
           (npages & VM_CORE_NPAGES);
}
*/

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
	paddr_t firstfree;

	lastpaddr = ram_getsize();
	firstfree = ram_getfirstfree();
	used_bytes = 0;
	raw_bytes = lastpaddr - firstfree;
	avail_bytes = raw_bytes / (1 + (float)sizeof(struct core_page) / PAGE_SIZE);
	page_max = avail_bytes / PAGE_SIZE;
	coremap_bytes = page_max * sizeof(struct core_page);

	coremap = (struct core_page *)firstfree;
	kprintf("coremap = %p\n", coremap);
	kprintf("coremap_top = %p\n", (char *)coremap + coremap_bytes);
	kprintf("lastpaddr = %p\n", (void *)lastpaddr);
    spinlock_init(&coremap_lock);
	(void)firstpaddr;
}

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	(void)npages;
	paddr_t pa = 0;

	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	(void)addr;
}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */

	return 0;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
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
