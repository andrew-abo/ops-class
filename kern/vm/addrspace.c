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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <spl.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
 * Allocates and intializes an empty addrspace struct.
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->next_segment = 0;
	for (int p = 0; p < 1<<VPN_BITS_PER_LEVEL; p++) {
		as->pages0[p] = NULL;
	}
	as->vheaptop = 0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

/*
 * Descend multi-level page table and print contents.
 */
void
dump_page_table(void **pages, int level) 
{
	const char *tab[] = {"", "     ", "          ", "               "};
	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
        if (pages[idx] == NULL) {
			continue;
		}		
		if (level == PT_LEVELS - 1) {
            kprintf("%s[%2d] 0x%x: 0x%x\n", tab[level], idx, 
			  ((struct pte **)pages)[idx]->paddr, 
			  ((struct pte **)pages)[idx]->status);
			continue;
        }
		kprintf("%s[%2d]-+\n", tab[level], idx);
		kprintf("%s     V\n", tab[level]);
		dump_page_table(pages[idx], next_level);
	}
}

/*
 * Descend multi-level page table and free dynamic memory.
 */
static void
destroy_page_table(void **pages, int level) 
{
	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
        if (pages[idx] == NULL) {
			continue;
		}		
		if (level == PT_LEVELS - 1) {
            kfree(pages[idx]);
			continue;
        }
		destroy_page_table(pages[idx], next_level);
	}
	if (level > 0) {
        kfree(pages);
	}
}

/*
 * Frees all dynamic memory associated with page table in addrspace as.
 */
/*
static void
destroy_page_table(struct addrspace *as)
{
	struct pte ****pages1;
	struct pte ***pages2;
	struct pte **pages3;

    for (int p0 = 0; p0 < (1<<PT_LEVEL0_BITS); p0++) {
		pages1 = as->pages0[p0];
		if (pages1 != NULL)	{
			for (int p1 = 0; p1 < (1<<PT_LEVEL1_BITS); p1++) {
				pages2 = pages1[p1];
				if (pages2 != NULL) {
					for (int p2 = 0; p2 < (1<<PT_LEVEL2_BITS); p2++) {
						pages3 = pages2[p2];
						if (pages3 != NULL) {
							for (int p3 = 0; p3 < (1<<PT_LEVEL3_BITS); p3++) {
								if (pages3[p3] != NULL) {
									kfree(pages3[p3]);
								}
							}
						}
					}
				}
			}
		}
	}
}
*/

/*
 * Frees all coremap pages belonging to this addrspace.
 */
void
as_destroy(struct addrspace *as)
{
    free_addrspace(as);
	destroy_page_table(as->pages0, 0);
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	// Invalidate all TLB entries from previous process.
	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 * 
 * No error checking for overlapping segments.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	int s;

	if (as->next_segment > SEGMENT_MAX) {
		return ENOMEM;
	}
    s = as->next_segment++;
	as->segments[s].vbase = vaddr;
	as->segments[s].size = memsize;
	as->segments[s].access = (readable ? VM_SEGMENT_READABLE : 0) | 
	                         (writeable ? VM_SEGMENT_WRITEABLE|VM_SEGMENT_WRITEABLE_ACTUAL : 0) |
							 (executable ? VM_SEGMENT_EXECUTABLE : 0);
	return 0;
}

/*
 * Temporarily enables write to all segments so they can be loaded without fault.
 */
int
as_prepare_load(struct addrspace *as)
{
	for (int s = 0; s < as->next_segment; s++) {
		as->segments[s].access |= VM_SEGMENT_WRITEABLE;
	}
	return 0;
}

/* 
 * Restores segments original writeable flags once load is complete.
 */
int
as_complete_load(struct addrspace *as)
{
	for (int s = 0; s < as->next_segment; s++) {
        as->segments[s].access &= ~VM_SEGMENT_WRITEABLE;
		if (as->segments[s].access & VM_SEGMENT_WRITEABLE_ACTUAL) {
			as->segments[s].access |= VM_SEGMENT_WRITEABLE;
		}
	}
	return 0;
}

/*
 * Defines a stack region and initializes stack pointer.
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	vaddr_t stack_bottom;
	size_t stack_size;

    // Define the stack as a generic segment.
	// We only declare segmentation fault if user access outside this max stack
	// size.
	*stackptr = USERSTACK;
	stack_size = USER_STACK_PAGES * PAGE_SIZE;
	stack_bottom = USERSTACK - stack_size;
	as_define_region(as, stack_bottom, stack_size, 1/*read*/, 1/*write*/, 0/*exec*/);
	return 0;
}

/*
 * Defines a heap region and initializes heap top.
 */
int
as_define_heap(struct addrspace *as)
{
	vaddr_t top = 0;
	vaddr_t segment_top;

	// Places heap above last segment.
	for (int p = 0; p < as->next_segment; p++) {
        segment_top = as->segments[p].vbase + as->segments[p].size;
		if ((segment_top < USERSTACK) && (segment_top > top)) {
			top = segment_top;
		}
	}
	// Align up to next page.
	as->vheaptop = (top + PAGE_SIZE - 1) & PAGE_FRAME;
	KASSERT(as->vheaptop / PAGE_SIZE + USER_HEAP_PAGES < USERSTACK - USER_STACK_PAGES);
	return 0;
}

/*
 * Returns 1 if address in a valid segment and operation is allowed, else 0.
 *
 * Args:
 *   as: Pointer to addrspace with defined segments.
 *   vaddr: Virtual address to check.
 *   read_request: 1 if reading, else 0.
 * 
 * Returns:
 *   1 if vaddr in as->segments[] and read_request matches segment 
 *   permissions, else 0.
 */
int
as_operation_is_valid(struct addrspace *as, vaddr_t vaddr, int read_request)
{
	vaddr_t vbase;
	size_t size;

    for (int s = 0; s < as->next_segment; s++) {
		vbase = as->segments[s].vbase;
		size = as->segments[s].size;
		if ((vaddr >= vbase) && (vaddr < vbase + size)) {
			if (read_request) {
				return as->segments[s].access & VM_SEGMENT_READABLE ? 1 : 0;
			}
			return as->segments[s].access & VM_SEGMENT_WRITEABLE ? 1 : 0;
		}
	}
	return 0;
}

/*
 * Looks up vaddr in page table.  If page table entry does not exit
 * it will be created.  Returns pointer to page table entry.
 * 
 * Args:
 *   as: Pointer to addrspace.
 *   vaddr: Virtual address to find.
 * 
 * Returns:
 *   Pointer to corresponding page table entry if successful, else NULL.
 */
struct pte
*as_touch_pte(struct addrspace *as, vaddr_t vaddr)
{
	int idx;
	unsigned vpn;
	void **pages;
	void **next_pages;
	const unsigned mask[] = {0x1f<<15, 0x1f<<10, 0x1f<<5, 0x1f};
	const unsigned shift[] = {15, 10, 5, 0};
	struct pte *pte;
	int level;

	// Walk down to leaf page table.
	vpn = vaddr >> PAGE_OFFSET_BITS;
	pages = as->pages0;
	for (level = 0; level < PT_LEVELS - 1; level++) {
        idx = (vpn & mask[level]) >> shift[level];
        next_pages = pages[idx];
        if (next_pages == NULL) {
			// Allocate and install next level page table.
            next_pages = kmalloc(sizeof(void *) * (1 << VPN_BITS_PER_LEVEL));
            if (next_pages == NULL) {
                return NULL;
			}
			bzero(next_pages, sizeof(void *) * (1 << VPN_BITS_PER_LEVEL));
			pages[idx] = next_pages;
		}
		pages = next_pages;
	}
	// Look up PTE in leaf page table.
	idx = vpn & mask[level];
	pte = pages[idx];
	if (pte == NULL) {
		pte = kmalloc(sizeof(struct pte));
		if (pte == NULL) {
			return NULL;
		}
		pte->status = 0;
		pte->paddr = (paddr_t)NULL;
		pages[idx] = pte;
	}
	return pte;
}

