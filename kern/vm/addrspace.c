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
#include <synch.h>
#include <current.h>
#include <cpu.h>

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
	as->pages_lock = lock_create("pages");
	if (as->pages_lock == NULL) {
		kfree(as);
		return NULL;
	}
	as->heap_lock = lock_create("heap");
	if (as->heap_lock == NULL) {
		lock_destroy(as->pages_lock);
		kfree(as);
		return NULL;
	}
	as->next_segment = 0;
	// Create empty page table.
	for (int p = 0; p < 1<<VPN_BITS_PER_LEVEL; p++) {
		as->pages0[p] = NULL;
	}
	as->vheapbase = 0;
	as->vheaptop = 0;
	return as;
}

/*
 * Lazy copy page references not actual page data.
 */
int
lazy_copy_page(struct pte *dst_pte, struct addrspace *src, struct pte *src_pte)
{
    ref_count_t *ref_count;
	struct spinlock *ref_count_lock;

	KASSERT(lock_do_i_hold(src->pages_lock));

	// TODO(aabo): this is not sufficient.  if the original page is evicted, then
	// the copy paddr is stale.  Coremap only points to the original addrspace.
	// Need to share entire pte so copies stay in sync with original?
	// as->copy = NULL if this is not a copy, else points to original pte.
	// replaces ref_count pointer.
    dst_pte->status = src_pte->status;
    dst_pte->paddr = src_pte->paddr;
    dst_pte->block_index = src_pte->block_index;
    if (src_pte->ref_count_lock == NULL) {
        lock_release(src->pages_lock);
        ref_count = kmalloc(sizeof(ref_count_t));
        if (ref_count == NULL) {
            return ENOMEM;
        }
        ref_count_lock = kmalloc(sizeof(struct spinlock));
        if (ref_count_lock == NULL) {
            kfree(ref_count);
            return ENOMEM;
        }
		spinlock_init(ref_count_lock);
        lock_acquire(src->pages_lock);
		*ref_count = 1;
        src_pte->ref_count = ref_count;
		src_pte->ref_count_lock = ref_count_lock;
    }
    spinlock_acquire(src_pte->ref_count_lock);
    (*src_pte->ref_count)++;
    dst_pte->ref_count = src_pte->ref_count;
    dst_pte->ref_count_lock = src_pte->ref_count_lock;
    spinlock_release(src_pte->ref_count_lock);
	return 0;
}

/*
 * Copies src page table and all referenced physical pages to dst.
 *
 * New physical pages are allocated and independent copies of the
 * pages are made, so physical addresses are different, but
 * virtual addresses will be the same.  For example, to
 * copy everything:
 * 
 * copy_page_table(dst, src->pages0, 0, 0x0)
 * 
 * Caller is responsible for locking source page table.
 * 
 * Args:
 *   dst: Pointer to destination address space.
 *   src_pages: Pointer to source address space level0 page table.
 *   vpn: initial virtual page number (normally 0).
 * 
 * Returns:
 *   0 on success, else errno.
 */
static int
copy_page_table(struct addrspace *dst, 
                struct addrspace *src,
				void **src_pages, 
				int level, 
				vaddr_t vpn) 
{
	struct pte *dst_pte, *src_pte;
	vaddr_t vaddr, next_vpn;
	void **next_pages;
	int result;

	KASSERT(lock_do_i_hold(src->pages_lock));
	KASSERT(lock_do_i_hold(dst->pages_lock));
	
	// Recursive depth-first traversal of source page table.
	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
		next_vpn = (vpn << VPN_BITS_PER_LEVEL) | idx;
		if (level == PT_LEVELS - 1) {
			vaddr = next_vpn << PAGE_OFFSET_BITS;
			// Release src page table for possible eviction.
			lock_release(src->pages_lock);
			dst_pte = as_touch_pte(dst, vaddr);
			lock_acquire(src->pages_lock);
			if (dst_pte == NULL) {
				return ENOMEM;
			}
			src_pte = ((struct pte *)src_pages) + idx;
			KASSERT(src_pte != NULL);
			result = lazy_copy_page(dst_pte, src, src_pte);
			if (result) {
				return result;
			}
			// Reactivate write detection.
			vm_tlb_remove(vaddr);			
			continue;
        }
		next_pages = src_pages[idx];
        if (next_pages == NULL) {
			continue;
		}		
		result = copy_page_table(dst, src, next_pages, next_level, next_vpn);
		if (result) {
			return result;
		}
	}
	return 0;
}

int
as_copy(struct addrspace *src, struct addrspace **ret)
{
	struct addrspace *dst;
	int result;

	*ret = NULL;
	dst = as_create();
	if (dst == NULL) {
		return ENOMEM;
	}

	for (int s = 0; s < src->next_segment; s++) {
		dst->segments[s].vbase = src->segments[s].vbase;
		dst->segments[s].size = src->segments[s].size;
		dst->segments[s].access = src->segments[s].access;
	}
	dst->next_segment = src->next_segment;

	lock_acquire(src->heap_lock);
	dst->vheapbase = src->vheapbase;
	dst->vheaptop = src->vheaptop;
	lock_release(src->heap_lock);

	lock_acquire(src->pages_lock);
	lock_acquire(dst->pages_lock);
	result = copy_page_table(dst, src, src->pages0, 0, (vaddr_t)0x0);
	lock_release(dst->pages_lock);
	lock_release(src->pages_lock);

	if (result) {
		as_destroy(dst);
		return result;
	}
	*ret = dst;
	return 0;
}

// Helper for dump_page_table.
// Visits each page table entry using recursive depth-first traversal
// of page table
static void
visit_page_table(void **pages, int level, vaddr_t vpn) 
{
	struct pte *pte;
	const char *tab[] = {"", "     ", "          ", "               "};
	vaddr_t vaddr, next_vpn;
	void **next_pages;

	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
		next_vpn = (vpn << VPN_BITS_PER_LEVEL) | idx;
		if (level == PT_LEVELS - 1) {
			vaddr = next_vpn << PAGE_OFFSET_BITS;
			pte = ((struct pte *)pages) + idx;
			// TODO(aabo): Unify some page table walking functions to reduce 
			// code duplication.
            kprintf("%s[%2d] v0x%08x -> p0x%08x: status=0x%x, block_index=%u\n", tab[level], 
			  idx, vaddr, pte->paddr, pte->status, pte->block_index);
			continue;
        }
		next_pages = pages[idx];
        if (next_pages == NULL) {
			continue;
		}
		kprintf("%s[%2d]-v\n", tab[level], idx);
		visit_page_table(next_pages, next_level, next_vpn);
	}
}

/*
 * Descend multi-level page table belonging to address space as 
 * and print contents.
 */
void
dump_page_table(struct addrspace *as)
{
	lock_acquire(as->pages_lock);
    visit_page_table(as->pages0, 0, 0x0);
	lock_release(as->pages_lock);
}

/*
 * Helper function for as_validate_page_table.
 *
 * Args:
 *   as: Pointer to address space to validate.
 *   pages: Pointer to page table.
 *   level: Current level of table to traverse starting at 0.
 *   vpn: Virtual page number (no page offset bits) .
 */
static void
validate_page_table(struct addrspace *as, void **pages, int level, vaddr_t vpn) 
{
	struct pte *pte;
	vaddr_t vaddr, next_vpn;
	void **next_pages;

	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
		next_vpn = (vpn << VPN_BITS_PER_LEVEL) | idx;
		if (level == PT_LEVELS - 1) {
			vaddr = next_vpn << PAGE_OFFSET_BITS;
			pte = ((struct pte *)pages) + idx;
			if (pte->status & VM_PTE_VALID) {
                KASSERT(vm_get_as(pte->paddr) == as);
                KASSERT(vm_get_vaddr(pte->paddr) == vaddr);
			}
			continue;
        }
		next_pages = pages[idx];
        if (next_pages == NULL) {
			continue;
		}
		validate_page_table(as, next_pages, next_level, next_vpn);
	}
}

/*
 * Validates page table is consistent with address space and coremap.
 *
 * Caller is responsible for locking page table.
 *
 * Returns:
 *   0 if valid, else panics.
 */
int
as_validate_page_table(struct addrspace *as)
{
	KASSERT(lock_do_i_hold(as->pages_lock));
	validate_page_table(as, as->pages0, 0, 0x0);
    return 0;
}

/*
 * Descend multi-level page table and free dynamic memory
 * used by table iteself and coremap pages referenced
 * by page table.  To destroy table from the top:
 * 
 * destroy_page_table(as->pages0, 0)
 * 
 * Args:
 *   pages: Pointer to array of pages, e.g. as->pages0.
 *   level: Starting level to descend from, e.g. 0.
 */
static void
destroy_page_table(void **pages, int level) 
{
	struct pte *pte;

	int next_level = level + 1;
	for (int idx = 0; idx < 1 << VPN_BITS_PER_LEVEL; idx++) {
		if (level == PT_LEVELS - 1) {
			pte = ((struct pte *)pages) + idx;
			if (pte->ref_count_lock != NULL) {
                spinlock_acquire(pte->ref_count_lock);
                if (*pte->ref_count == 1) {
                    spinlock_release(pte->ref_count_lock);
                    kfree(pte->ref_count);
                    kfree(pte->ref_count_lock);
                } else {
                    (*pte->ref_count)--;
                    spinlock_release(pte->ref_count_lock);
					continue;
                }
			}
			if (pte->status & VM_PTE_VALID) {
                free_pages(pte->paddr);
            }
			if (pte->status & VM_PTE_BACKED) {
                free_swapmap_block(pte->block_index);
			}
			continue;
        }
        if (pages[idx] == NULL) {
			continue;
		}
		destroy_page_table(pages[idx], next_level);
	}
	if (level > 0) {
        kfree(pages);
	}
}

/*
 * Destroy all dynamic memory associated with this address space
 * including page table and coremap pages.
 */
void
as_destroy(struct addrspace *as)
{
	KASSERT(as != NULL);

	// Follow VM locking order to avoid another process trying to evict pages
	// from the addrspace we are destroying.

	lock_acquire_evict();
	lock_acquire(as->pages_lock);
	destroy_page_table(as->pages0, 0);
	lock_release(as->pages_lock);
	lock_destroy(as->pages_lock);
	KASSERT(!lock_do_i_hold(as->heap_lock));
	lock_destroy(as->heap_lock);
	kfree(as);
	lock_release_evict();
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}
	vm_tlb_erase();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}
	vm_tlb_erase();	
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. 
 * Segments are forced to be page aligned.
 * The segment in memory extends from the page containing VADDR up 
 * through the page containing VADDR+MEMSIZE.
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
	as->segments[s].vbase = vaddr & PAGE_FRAME;
	as->segments[s].size = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
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
	// TODO(aabo): We will still get 1 unnecessary fault since pages are initially
	// installed in TLB as dirty=0 (read-only).  We could have some
	// extra state that installs them as dirty=1 when loading.
	// then clears dirty from TLB in as_complete_load.
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
	// TODO(aabo): set dirty=0 for all TLB entries.
	// If we leave dirty=1 (write enable) on pages that are
	// actually read only, we won't detect any write faults.
	// If a page really is writeable or modified, we have
	// that state already saved in page table or coremap.
	// when a write occurs, it will get flagged as dirty,
	// and write enabled which is redundant but ok.
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
	as->vheapbase = (top + PAGE_SIZE - 1) & PAGE_FRAME;
	as->vheaptop = as->vheapbase;
	KASSERT(as->vheapbase / PAGE_SIZE + USER_HEAP_PAGES < USERSTACK - USER_STACK_PAGES);
	return 0;
}

// Prints out addrspace segments for debugging.
void 
dump_segments(struct addrspace *as)
{
	vaddr_t vbase;
	size_t size;

    for (int s = 0; s < as->next_segment; s++) {
		vbase = as->segments[s].vbase;
		size = as->segments[s].size;
		kprintf("Segment %d\n", s);
		kprintf("vbase = 0x%08x\n", vbase);
		kprintf("vtop  = 0x%08x\n\n", vbase + size);
	}
}

/*
 * Returns 1 if address in a valid segment and operation is allowed, else 0.
 *
 * Args:
 *   as: Pointer to addrspace with defined segments.
 *   vaddr: Virtual address to check.
 *   read_request: 1 if reading, 0 if writing, -1 if either.
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
	int valid;

    for (int s = 0; s < as->next_segment; s++) {
		vbase = as->segments[s].vbase;
		size = as->segments[s].size;
		if ((vaddr >= vbase) && (vaddr < vbase + size)) {
			if (read_request > 0) {
				return as->segments[s].access & VM_SEGMENT_READABLE ? 1 : 0;
			} else if (read_request == 0) {
                return as->segments[s].access & VM_SEGMENT_WRITEABLE ? 1 : 0;
			}
			return as->segments[s].access & (VM_SEGMENT_READABLE || 
			  VM_SEGMENT_WRITEABLE) ? 1 : 0;
		}
	}
	// Heap is a special segment not stored in as->segments[].
	valid = 0;
	lock_acquire(as->heap_lock);
	if ((vaddr >= as->vheapbase) && (vaddr < as->vheaptop)) {
        valid = 1;
    }
	lock_release(as->heap_lock);
	return valid;
}

/*
 * Looks up vaddr in page table.
 * 
 * Look up an existing pte or create if does not exist.  Does
 * not allocate any physical pages.
 *  
 * Caller is responsible for locking page table.
 * 
 * Args:
 *   as: Pointer to addrspace.
 *   vaddr: Virtual address to find.
 *   create: Creates missing levels and table entries if 1, else
 *     sets pte_ptr = NULL when encounters a missing level or entry.
 *   pte_ptr: Pointer to return pointer to page table entry.
 * 
 * Returns:
 *  0 on success. pte_ptr will refer to the table entry or NULL if not found.
 *  Else errno value if there is a system error.
 */
static int 
touch_pte(struct addrspace *as, vaddr_t vaddr, int create, struct pte **pte_ptr)
{
	int idx;
	unsigned vpn;
	void **pages;
	void **next_pages;
	struct pte *leaf_pages;
	const unsigned mask[] = {0x1f<<15, 0x1f<<10, 0x1f<<5, 0x1f};
	const unsigned shift[] = {15, 10, 5, 0};
	int level;
	
	KASSERT(lock_do_i_hold(as->pages_lock));

	// Walk down to leaf page table.
	vpn = vaddr >> PAGE_OFFSET_BITS;
	pages = as->pages0;
	*pte_ptr = NULL;
	for (level = 0; level < PT_LEVELS - 2; level++) {
        idx = (vpn & mask[level]) >> shift[level];
        next_pages = pages[idx];
        if (next_pages == NULL) {
			if (!create) {
				// vaddr not found.
				return 0;
			}
			// Allocate and install next level page table.
			// See VM locking order at top of vm.c.
			lock_release(as->pages_lock);
            next_pages = kmalloc(sizeof(void *) * (1 << VPN_BITS_PER_LEVEL));
			lock_acquire(as->pages_lock);
            if (next_pages == NULL) {
                return ENOMEM;
			}
			bzero(next_pages, sizeof(void *) * (1 << VPN_BITS_PER_LEVEL));
			pages[idx] = next_pages;
		}
		pages = next_pages;
	}

	// Get base address of leaf table.
	idx = (vpn & mask[level]) >> shift[level];
	leaf_pages = (struct pte *)(pages[idx]);
	if (leaf_pages == NULL) {
        if (!create) {
            // vaddr not found.
            return 0;
        }
        // Release page table before potential eviction.
        lock_release(as->pages_lock);
        leaf_pages = kmalloc(sizeof(struct pte) * (1 << VPN_BITS_PER_LEVEL));
        lock_acquire(as->pages_lock);
		if (leaf_pages == NULL) {
			return ENOMEM;
		}
		// Install and initialize leaf ptes.
        pages[idx] = leaf_pages;
        for (idx = 0; idx < (1 << VPN_BITS_PER_LEVEL); idx++) {
            leaf_pages[idx].status = 0;
            leaf_pages[idx].block_index = 0;
            leaf_pages[idx].paddr = (paddr_t)NULL;
			leaf_pages[idx].ref_count = NULL;
			leaf_pages[idx].ref_count_lock = NULL;
        }
    }
	level++;
	idx = (vpn & mask[level]) >> shift[level];
    *pte_ptr = leaf_pages + idx;
    return 0;
}

/*
 * Look up an existing page table entry or create if does not exist.
 * 
 * Caller is responsible for locking as->pages_lock.
 *  
 * Args:
 *   as: Pointer to addrspace.
 *   vaddr: Virtual address to find.
 * 
 * Returns:
 *   Pointer to pte else NULL if could not find and create.
 */

struct pte
*as_touch_pte(struct addrspace *as, vaddr_t vaddr)
{
	int result;
	struct pte *pte;

	result = touch_pte(as, vaddr, /*create=*/1, &pte);
	if (result) {
		return NULL;
	}
	return pte;
}

/*
 * Read-only look up of page table entry by vaddr.
 *
 * Caller is responsible for locking as->pages_lock.
 * 
 * Returns:
 *   Pointer to pte or NULL if not found.
 */
struct pte
*as_lookup_pte(struct addrspace *as, vaddr_t vaddr)
{
	int result;
	struct pte *pte;

	result = touch_pte(as, vaddr, /*create=*/0, &pte);
	KASSERT(result == 0);
	if (pte == NULL) {
		return NULL;
	}
	return pte;
}

/*
 * Frees physical page corresponding to vaddr.
 * If page does not exist do nothing.
 * 
 * Args:
 *   as: Pointer to address space to modify.
 *   vaddr: Virtual address of page to destroy.
 */
void
as_destroy_page(struct addrspace *as, vaddr_t vaddr)
{
	struct pte *pte;

	lock_acquire(as->pages_lock);
	pte = as_lookup_pte(as, vaddr);
	if (pte == NULL) {
		// Silently ignores non-existent pages.
		lock_release(as->pages_lock);
		return;
	}
	if (pte->ref_count_lock != NULL) {
        spinlock_acquire(pte->ref_count_lock);
        if (*pte->ref_count == 1) {
            spinlock_release(pte->ref_count_lock);
            kfree(pte->ref_count);
            kfree(pte->ref_count_lock);
			pte->ref_count = NULL;
			pte->ref_count_lock = NULL;
        } else {
            (*pte->ref_count)--;
            spinlock_release(pte->ref_count_lock);
	        pte->status = 0;
	        pte->block_index = 0;
	        pte->paddr = (paddr_t)NULL;
	        lock_release(as->pages_lock);
			return;
        }
	}
	if (pte->status & VM_PTE_VALID) {
        free_pages(pte->paddr);
	}
	if (pte->status & VM_PTE_BACKED) {
        free_swapmap_block(pte->block_index);
	}
	pte->status = 0;
	pte->block_index = 0;
	pte->paddr = (paddr_t)NULL;
	lock_release(as->pages_lock);
}
