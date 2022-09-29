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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include <types.h>
#include "opt-dumbvm.h"

struct vnode;


/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */

#define SEGMENT_MAX 10  // Memory segments per process.

// These values can be arbitrarily large since they are virtual,
// however, we make them finite so we can treat heap
// and stack like any other segment.
#define USER_STACK_PAGES 1024 // Max size of user stack in pages.
#define USER_HEAP_PAGES 16384  // Max size of user heap in pages.

// Segment permissions.
#define VM_SEGMENT_READABLE 0x1
#define VM_SEGMENT_WRITEABLE 0x2  // (Temporary) write enable.
#define VM_SEGMENT_EXECUTABLE 0x4
// Backup copy of true write enable we can use to restore temporary
// write enable after load operations during which we temporarily
// enable writes.
#define VM_SEGMENT_WRITEABLE_ACTUAL 0x8 

// Virtual address bits per page table level.
//  32b vaddr = 20b VPN + 12b page offset
//  VPN = 5b + 5b + 5b + 5b
#define PT_LEVELS 4
#define VPN_BITS 20
#define PAGE_OFFSET_BITS 12
#define VPN_BITS_PER_LEVEL (VPN_BITS / PT_LEVELS)

struct segment {
    vaddr_t vbase;  // Starting virtual address.
    size_t size;  // Size in bytes.
    int access;  // Segment permissions.  See flags above.
};

// Note: VALID and BACKED will both be zero for first page access.
#define VM_PTE_VALID 0x1  // Page in memory.
#define VM_PTE_BACKED 0x2  // Page on disk.  

// Page table entry.
// We don't store the virtual address which is inherently coded in the indices
// of the multi-level page tables.
struct pte {
    uint32_t status;
    paddr_t paddr;
    unsigned block_index;  // Page number offset on swap disk.
};

struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t as_npages2;
        paddr_t as_stackpbase;
#else
        struct segment segments[SEGMENT_MAX];
        int next_segment;  // Next segment index to populate.
        void *pages0[1<<VPN_BITS_PER_LEVEL];  // Level0 page table.
        struct lock *pages_lock;  // Page table lock.
        vaddr_t vheapbase;  // Starting address of heap.
        vaddr_t vheaptop;  // Current top of heap.
        struct lock *heap_lock;
#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);
int               as_define_heap(struct addrspace *as);


/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

int as_operation_is_valid(struct addrspace *as, vaddr_t vaddr, int read_request);
struct pte *as_touch_pte(struct addrspace *as, vaddr_t vaddr);
struct pte *as_lookup_pte(struct addrspace *as, vaddr_t vaddr);
void dump_page_table(struct addrspace *as);
void dump_segments(struct addrspace *as);
struct pte *as_create_page(struct addrspace *as, vaddr_t vaddr);
void as_destroy_page(struct addrspace *as, vaddr_t vaddr);
int as_validate_page_table(struct addrspace *as);
void vm_can_sleep(void);

#endif /* _ADDRSPACE_H_ */
