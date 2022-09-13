/*
 * addrspacetest.c
 *
 * Tests for address space functions.
 * 
 */

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <test.h>
#include <kern/test161.h>

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
    pte0 = as_create_page(as, 0x00000000);
    KASSERT(pte0 != NULL);

    pte1 = as_touch_pte(as, 0x00000000);
    KASSERT(pte0 == pte1);

    pte0 = as_create_page(as, 0x00001000);
    KASSERT(pte0 != NULL);

    // Tests address within same page maps to same pte.
    pte0 = as_create_page(as, 0x00007000);
    pte1 = as_touch_pte(as, 0x00007001);
    KASSERT(pte0 != NULL);
    KASSERT(pte0 == pte1);

    pte0 = as_create_page(as, 0x0001f000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(as, 0x00020000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(as, 0x00021000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(as, 0x003e0000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(as, 0x07c00000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(as, 0x7f000000);
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

    kprintf("Starting as9 test...\n");
    as = as_create();
    KASSERT(as != NULL);
    pte0 = as_create_page(as, 0x00040000);
    KASSERT(pte0 != NULL);
    as_destroy_page(as, 0x00040000);
    as_destroy(as);
	success(TEST161_SUCCESS, SECRET, "as8");

	return 0;
}

#define TEST_PAGES 1000
#define CREATE_CYCLES 10

// Tests as_copy correctly copies page table
// entries and physical pages.
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
    offset = random() % 0x1000;
    stride = random() % 0x100;
    kprintf("create %d pages\n", n1_create_pages);
    for (i = 0; i < n1_create_pages; i++) {
        pte = as_create_page(as, (offset + i * stride) * PAGE_SIZE);
        KASSERT(pte != NULL);
    }

    n_free_pages = random() % n1_create_pages;
    for (i = 0; i < n_free_pages; i++) {
        // May generate repeats which will attempt to destroy a page
        // more than once, but that should be silently ignored.
        k = random() % n1_create_pages;
        as_destroy_page(as, (offset + k * stride) * PAGE_SIZE);
    }

    offset += n1_create_pages * stride + random() % 0x1000;
    stride = random() % 0x100;
    n2_create_pages = random() % TEST_PAGES;
    for (i = 0; i < n2_create_pages; i++) {
        pte = as_create_page(as, (offset + i * stride) * PAGE_SIZE);
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

    kprintf("Starting as10 test...\n");

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

    kprintf("Starting as11 test...\n");

    as = as_create();
    KASSERT(as != NULL);

    // Exhaust user memory.
    for (i = 0; i < MAX_PAGES; i++) {
        pte = as_create_page(as, i * PAGE_SIZE);
        if (pte == NULL) {
            break;
        }
    }
    KASSERT(pte == NULL);

    // Free one page at front
    as_destroy_page(as, 0);
    pte = as_create_page(as, MAX_PAGES * PAGE_SIZE);
    KASSERT(pte != NULL);

    // Should fail since we are full again.
    pte = as_create_page(as, (MAX_PAGES + 1) * PAGE_SIZE);
    KASSERT(pte == NULL);

    // Free one page at back.
    as_destroy_page(as, (i-1) * PAGE_SIZE);
    pte = as_create_page(as, (MAX_PAGES + 1) * PAGE_SIZE);
    KASSERT(pte != NULL);

    // Should fail since we are full again.
    pte = as_create_page(as, (MAX_PAGES + 2) * PAGE_SIZE);
    KASSERT(pte == NULL);

    as_destroy(as);

	success(TEST161_SUCCESS, SECRET, "as10");
	return 0;
}

