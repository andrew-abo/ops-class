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

#ifndef _VM_H_
#define _VM_H_

/*
 * Machine-independent virtual memory interface.
 *
 */


#include <machine/vm.h>
#include <types.h>
#include <addrspace.h>
#include "opt-vm_perf.h"

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

// Bit masks for core_page status.
#define VM_CORE_USED 0x10000  // Page is allocated and in use.
#define VM_CORE_ACCESSED 0x20000  // Page has been accessed since last eviction sweep.
#define VM_CORE_DIRTY 0x40000  // Page in memory differs from page on disk.
#define VM_CORE_SHARED 0x80000  // Page is shared between addrspaces.
#define VM_CORE_NPAGES 0xffff  // Mask for number of contiguous pages in this allocation
                            // starting at current index.

struct core_page {
    uint32_t status;  // See bit masks above.
    vaddr_t vaddr;    // Virtual address where this page starts.
    struct pte *pte;  // Pointer to shared page table entry if
                          // status & VM_CORE_SHARED != 0. 'as' is not
                          // meaningful when multiple addrspaces share
                          // a page.
    struct addrspace *as;  // Pointer to address space this page belongs to.
                           // NULL if owned by kernel or shared page. 
    unsigned prev;  // Index of previous block in coremap.
};

// Initializes physical memory map to enable kmalloc.
void vm_init_coremap(void);

// Test and debug support.
void lock_and_dump_coremap(void);
int as_in_coremap(struct addrspace *as);
int validate_coremap(void);

// Initialize swap system.
void vm_bootstrap(void);

// Do not use: for testing only.
int set_swap_enabled(int enabled);

// Read/write pages from/to swap disk.
int block_write(unsigned block_index, paddr_t paddr);
int block_read(unsigned block_index, paddr_t paddr);
int get_page_via_table(struct addrspace *as, vaddr_t faultaddress);
void free_swapmap_block(int block_index);
size_t swap_used_pages(void);
int save_page(struct pte *pte, int dirty);
int restore_page(struct addrspace *as, struct pte *pte, vaddr_t vaddr);

/* Fault handling function called by trap code */
int handle_write_fault(struct addrspace *as, vaddr_t faultaddress);
int vm_fault(int faulttype, vaddr_t faultaddress);
paddr_t locking_find_victim_page(void);
int evict_page(unsigned *coremap_index);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t vaddr);

/* Allocate/free coremap pages */
paddr_t alloc_pages(unsigned npages);
void free_pages(vaddr_t vaddr);
void vm_tlb_erase(void);
void vm_tlb_remove(vaddr_t vaddr);
unsigned paddr_to_core_idx(paddr_t paddr);
paddr_t core_idx_to_paddr(unsigned p);
paddr_t coremap_assign_to_kernel(unsigned p, unsigned npages);
unsigned coremap_assign_vaddr(paddr_t paddr, struct pte *pte, vaddr_t vaddr);

struct addrspace *vm_get_as(paddr_t paddr);
vaddr_t vm_get_vaddr(paddr_t paddr);
void spinlock_acquire_coremap(void);
void spinlock_release_coremap(void);
void lock_acquire_evict(void);
void lock_release_evict(void);

/*
 * Return amount of memory (in bytes) used by allocated coremap pages.  If
 * there are ongoing allocations, this value could change after it is returned
 * to the caller. But it should have been correct at some point in time.
 */
unsigned int coremap_used_bytes(void);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

#if OPT_VM_PERF
void reset_vm_perf(void);
void count_tlb_fault(void);
void count_swap_in(void);
void count_swap_out(void);
void count_fault(void);
void count_eviction(void);
void dump_vm_perf(void);
#endif

#endif /* _VM_H_ */
