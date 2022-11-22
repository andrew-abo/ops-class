/*
 * addrspacetest.c
 *
 * Tests for address space functions.
 * 
 */

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <vm.h>

/*
 * Only for directed testing purposes.
 *
 * Creates a new physical and corresponding virtual page.
 *
 * Args:
 *   as: Pointer to address space to create page in.
 *   vaddr: Virtual address to assign.
 * 
 * Returns:
 *   Pointer to page table entry, else NULL if unsuccessful.
 */
struct pte 
*create_test_page(struct addrspace *as, vaddr_t vaddr)
{
    paddr_t paddr;
	struct pte *pte;
	
	vaddr &= PAGE_FRAME;
	// Must be a user space virtual address.
	KASSERT(vaddr < MIPS_KSEG0);

	lock_acquire(as->pages_lock);
    pte = as_touch_pte(as, vaddr);
    if (pte == NULL) {
		lock_release(as->pages_lock);
        return NULL;
    }
	// Page must not already exist.
	KASSERT((pte->status == 0) && (pte->paddr == (paddr_t)NULL));	
    lock_release(as->pages_lock);

    paddr = alloc_pages(1);
    if (paddr == (paddr_t)NULL) {
        return NULL;
    }
    lock_acquire(as->pages_lock);
    spinlock_acquire_coremap();
    coremap_assign_vaddr(paddr, pte, vaddr);
    pte->paddr = paddr;
	pte->status = VM_PTE_VALID;
	lock_release(as->pages_lock);
    spinlock_release_coremap();

    return pte;
}


// Tests addrspace can be created and destroyed.
int
addrspacetest1(int nargs, char **args)
{
	(void)nargs;
	(void)args;
	struct addrspace *as;

	kprintf("Starting as1 test...\n");
	as = as_create();
    KASSERT(as != NULL);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as1");

	return 0;
}

// Tests multiple addrspace segments can be defined.
int
addrspacetest2(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;

    kprintf("Starting as2 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, 0x00010001, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00020020, 0x2331, 1, 0, 1);
    as_define_region(as, 0x00030300, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00040000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00050000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00060000, 0x9990, 1, 0, 1);
    // Segments are page aligned.
    KASSERT(as->segments[0].vbase == 0x00010000);
    KASSERT(as->segments[0].size == 0x3000);
    KASSERT(as->segments[0].access & VM_SEGMENT_READABLE);
    KASSERT(as->segments[0].access & VM_SEGMENT_WRITEABLE);
    KASSERT(!(as->segments[0].access & VM_SEGMENT_EXECUTABLE));
    KASSERT(as->segments[5].vbase == 0x00060000);
    KASSERT(as->segments[5].size == 0xa000);
    KASSERT(as->segments[5].access & VM_SEGMENT_READABLE);
    KASSERT(!(as->segments[5].access & VM_SEGMENT_WRITEABLE));
    KASSERT(as->segments[5].access & VM_SEGMENT_EXECUTABLE);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as2");

	return 0;
}

// Tests as_prepare/complete_load can enable/restore write permission.
int
addrspacetest3(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;

    kprintf("Starting as3 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, 0x00010000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00020000, 0x2331, 1, 0, 1);
    as_define_region(as, 0x00030000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00040000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00050000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00060000, 0x9990, 1, 0, 1);
    as_prepare_load(as);
    for (int s = 0; s < 6; s++) {
        KASSERT(as->segments[s].access & VM_SEGMENT_WRITEABLE);
    }
    as_complete_load(as);
    KASSERT(as->segments[0].access & VM_SEGMENT_WRITEABLE);
    KASSERT(!(as->segments[1].access & VM_SEGMENT_WRITEABLE));
    KASSERT(as->segments[2].access & VM_SEGMENT_WRITEABLE);
    KASSERT(as->segments[3].access & VM_SEGMENT_WRITEABLE);
    KASSERT(as->segments[4].access & VM_SEGMENT_WRITEABLE);
    KASSERT(!(as->segments[5].access & VM_SEGMENT_WRITEABLE));
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as3");

	return 0;
}

// Tests heap is created after all other segments.
int
addrspacetest4(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;

    kprintf("Starting as4 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, 0x00010000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00020000, 0x2331, 1, 0, 1);
    as_define_region(as, 0x00030000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00040000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00050000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00060000, 0x9990, 1, 0, 1);
    as_define_heap(as);
    KASSERT(as->vheaptop > 0x00060000);
    KASSERT((as->vheaptop & PAGE_FRAME) == as->vheaptop);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as4");

	return 0;
}

// Tests stack is created.
int
addrspacetest5(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;
    vaddr_t stack;

    kprintf("Starting as5 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    as_define_stack(as, &stack);
    KASSERT(stack == USERSTACK);    
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as5");

	return 0;
}

// Tests as_operation_is_valid() returns correct value.
int
addrspacetest6(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;

    kprintf("Starting as6 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, 0x10000, 0x2000, 1, 1, 0);
    as_define_region(as, 0x20000, 0x2331, 1, 0, 1);
    as_define_region(as, 0x30000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x40000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x50000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x60000, 0x9990, 1, 0, 1);
    KASSERT(as_operation_is_valid(as, 0x11000, 1));
    KASSERT(!as_operation_is_valid(as, 0x13000, 1));
    KASSERT(!as_operation_is_valid(as, 0x13000, 0));
    KASSERT(!as_operation_is_valid(as, 0x30000, 1));
    KASSERT(as_operation_is_valid(as, 0x10000, 0));
    KASSERT(!as_operation_is_valid(as, 0x20001, 0));    
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as6");

	return 0;
}

// Tests as_touch_pte() can create/store/load
// page table entries.
int
addrspacetest7(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;
    struct pte *pte0, *pte1;

    kprintf("Starting as7 test...\n");
    as = as_create();
    KASSERT(as != NULL);

    // Create and lookup a page at vaddr=0x0.
    pte0 = create_test_page(as, 0x00000000);
    KASSERT(pte0 != NULL);
    lock_acquire(as->pages_lock);
    pte1 = as_touch_pte(as, 0x00000000);
    lock_release(as->pages_lock);
    KASSERT(pte0 == pte1);

    pte0 = create_test_page(as, 0x00001000);
    KASSERT(pte0 != NULL);

    // Tests address within same page maps to same pte.
    pte0 = create_test_page(as, 0x00007000);
    lock_acquire(as->pages_lock);
    pte1 = as_touch_pte(as, 0x00007001);
    lock_release(as->pages_lock);
    KASSERT(pte0 != NULL);
    KASSERT(pte0 == pte1);

    pte0 = create_test_page(as, 0x00020000);
    KASSERT(pte0 != NULL);
    pte0 = create_test_page(as, 0x00021000);
    KASSERT(pte0 != NULL);
    pte0 = create_test_page(as, 0x003e0000);
    KASSERT(pte0 != NULL);
    pte0 = create_test_page(as, 0x07c00000);
    KASSERT(pte0 != NULL);
    pte0 = create_test_page(as, 0x7f000000);
    KASSERT(pte0 != NULL);

    // Tests level0 table has correct empty/non-empty entries.
    dump_page_table(as);
    KASSERT(as->pages0[0] != NULL);
    KASSERT(as->pages0[1] == NULL);
    KASSERT(as->pages0[2] == NULL);
    KASSERT(as->pages0[3] == NULL);
    KASSERT(as->pages0[4] == NULL);
    KASSERT(as->pages0[5] == NULL);
    KASSERT(as->pages0[8] == NULL);
    KASSERT(as->pages0[15] != NULL);
    KASSERT(as->pages0[31] == NULL);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as7");

	return 0;
}

// Tests as_destroy_page can destroy a page.
int
addrspacetest8(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;
    struct pte *pte0;

    kprintf("Starting as8 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    pte0 = create_test_page(as, 0x00040000);
    KASSERT(pte0 != NULL);
    as_destroy_vaddr(as, 0x00040000);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as8");

	return 0;
}

#define TEST_PAGES 100
#define CREATE_CYCLES 10

static void
create_and_free(struct addrspace *as)
{
    unsigned n_free_pages;
    unsigned n1_create_pages;
    unsigned n2_create_pages;
    unsigned i, k;
    struct pte *pte;
    unsigned offset;
    unsigned stride;

    KASSERT(as != NULL);

    n1_create_pages = 1 + random() % TEST_PAGES;
    offset = random() % 0x100;
    stride = 1 + random() % 0x100;
    kprintf("create %d pages\n", n1_create_pages);
    for (i = 0; i < n1_create_pages; i++) {
        //kprintf("create %d@ 0x%08x\n", i, (offset + i * stride) * PAGE_SIZE);
        pte = create_test_page(as, (offset + i * stride) * PAGE_SIZE);
        KASSERT(pte != NULL);
    }

    n_free_pages = random() % n1_create_pages;
    kprintf("free %d pages\n", n_free_pages);
    for (i = 0; i < n_free_pages; i++) {
        // May generate repeats which will attempt to destroy a page
        // more than once, but that should be silently ignored.
        k = random() % n1_create_pages;
        //kprintf("destroy %d@ 0x%08x\n", k, (offset + k * stride) * PAGE_SIZE);
        as_destroy_vaddr(as, (offset + k * stride) * PAGE_SIZE);
    }

    offset += n1_create_pages * stride + random() % 0x1000;
    stride = 1 + random() % 0x100;
    n2_create_pages = random() % TEST_PAGES;
    kprintf("create %d pages\n", n2_create_pages);
    for (i = 0; i < n2_create_pages; i++) {
        //kprintf("create %d@ 0x%08x\n", i, (offset + i * stride) * PAGE_SIZE);
        pte = create_test_page(as, (offset + i * stride) * PAGE_SIZE);
        KASSERT(pte != NULL);
    }
}

// Stress test create, free, destroy pages.
int
addrspacetest9(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;

    kprintf("Starting as9 test...\n");

    for (int i = 0; i < CREATE_CYCLES; i++) {
        kprintf("loop %d (0x%08x)\n", i, random());
        as = as_create();
        KASSERT(as != NULL);
        create_and_free(as);
        as_destroy(as);
    }

	success(TEST161_SUCCESS, SECRET, "as9");
	return 0;
}

#define MAX_PAGES 4096

// Allocate all pages.
int
addrspacetest10(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *as;
    struct pte *pte;
    int i;
    int old_swap_enabled;

    kprintf("Starting as10 test...\n");

    old_swap_enabled = set_swap_enabled(0);

    as = as_create();
    KASSERT(as != NULL);

    // Exhaust user memory.
    for (i = 0; i < MAX_PAGES; i++) {
        pte = create_test_page(as, i * PAGE_SIZE);
        if (pte == NULL) {
            break;
        }
    }
    KASSERT(pte == NULL);

    // Free 1 page for user.  Free 1 page for kernel page table entry.
    as_destroy_vaddr(as, 0x0000);
    as_destroy_vaddr(as, 0x1000);

    // Should be able to get a free page now.
    pte = create_test_page(as, MAX_PAGES * PAGE_SIZE);
    KASSERT(pte != NULL);
    as_destroy(as);

    set_swap_enabled(old_swap_enabled);

	success(TEST161_SUCCESS, SECRET, "as10");
	return 0;
}

// Allocate all pages then copy addrspace.
int
addrspacetest11(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *src, *dst;
    struct pte *src_pte, *dst_pte;
    vaddr_t vaddr;
    vaddr_t src_kvaddr, dst_kvaddr;
    unsigned i, j;
    int result;
	size_t swap0, swap1;
	size_t mem0, mem1;
	const unsigned test_pages = 64;

    kprintf("Starting as11 test...\n");

	mem0 = coremap_used_bytes();
	swap0 = swap_used_pages();

    src = as_create();
    KASSERT(src != NULL);

    kprintf("Create pages\n");
    for (i = 0; i < test_pages; i++) {
		if (i % 64 == 0) {
			kprintf("\n");
		}
		kprintf(".");
        vaddr = i * PAGE_SIZE;
        src_pte = create_test_page(src, vaddr);
        if (src_pte == NULL) {
            break;
        }
        KASSERT(src_pte != NULL);
        src_kvaddr = PADDR_TO_KVADDR(src_pte->paddr);
        *(unsigned *)src_kvaddr = i;
    }
    kprintf("\n");

    result = as_copy(src, &dst);
    KASSERT(result == 0);

    // Check if copy matches source.
    lock_acquire(src->pages_lock);
    lock_acquire(dst->pages_lock);
    kprintf("Check page tables\n");
    for (j = 0; j < i; j++) {
		if (j % 64 == 0) {
			kprintf("\n");
		}
		kprintf(".");
        vaddr = j * PAGE_SIZE;
        src_pte = as_lookup_pte(src, vaddr);
        KASSERT(src_pte != NULL);
        dst_pte = as_lookup_pte(dst, vaddr);
        KASSERT(dst_pte != NULL);
        // We would like to access all the virtual addresses
        // and see if page faults retrieve the correct contents
        // either from memory or swap.  But, since this is a 
        // kernel test, there is no active addrspace, so we
        // can't test vm_fault logic.
        // We can check direct mapped addresses have matching data.
        src_kvaddr = PADDR_TO_KVADDR(src_pte->paddr);
        dst_kvaddr = PADDR_TO_KVADDR(dst_pte->paddr);
        KASSERT(*(unsigned *)dst_kvaddr == j);
        KASSERT(*(unsigned *)src_kvaddr == *(unsigned *)dst_kvaddr);
        // Tests copy is by reference only.
        KASSERT(src_pte->paddr == dst_pte->paddr);
    }
    lock_release(src->pages_lock);
    lock_release(dst->pages_lock);

    as_destroy(src);
    as_destroy(dst);

	// Verify memory and swap have been cleaned up.
	mem1 = coremap_used_bytes();
	swap1 = swap_used_pages();
	KASSERT(swap0 == swap1);
	KASSERT(mem0 == mem1);

	success(TEST161_SUCCESS, SECRET, "as11");
	return 0;
}

int
addrspacetest12(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    // TODO(aabo): remove
    // write unit tests for as_*_pte.
    return 0;
}

int
addrspacetest13(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    // TODO(aabo): remove
	return 0;
}
