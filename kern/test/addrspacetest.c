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
    as_define_region(as, 0x00010000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00020000, 0x2331, 1, 0, 1);
    as_define_region(as, 0x00030000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00040000, 0x2331, 1, 1, 0);
    as_define_region(as, 0x00050000, 0x2331, 0, 1, 0);
    as_define_region(as, 0x00060000, 0x9990, 1, 0, 1);
    KASSERT(as->segments[0].vbase == 0x00010000);
    KASSERT(as->segments[0].size == 0x2331);
    KASSERT(as->segments[0].access & VM_SEGMENT_READABLE);
    KASSERT(as->segments[0].access & VM_SEGMENT_WRITEABLE);
    KASSERT(!(as->segments[0].access & VM_SEGMENT_EXECUTABLE));
    KASSERT(as->segments[5].vbase == 0x00060000);
    KASSERT(as->segments[5].size == 0x9990);
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
    dump_page_table(as->pages0, 0);
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

// Tests as_copy correctly copies page table
// entries and physical pages.
int
addrspacetest8(int nargs, char **args)
{
    (void)nargs;
    (void)args;
    struct addrspace *src, *dst;
    struct pte *pte0, *pte1;
    int result;
    void **level1;
    void **level2;
    struct pte **level3;

    kprintf("Starting as8 test...\n");
    src = as_create();
    KASSERT(src != NULL);

    pte0 = as_create_page(src, 0x00010000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x00020000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x00030000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x00400000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x00510000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x06000000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x07000000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x0a000000);
    KASSERT(pte0 != NULL);
    pte0 = as_create_page(src, 0x7f000000);
    KASSERT(pte0 != NULL);
    // Write some data to copy.
    *((uint32_t *)PADDR_TO_KVADDR(pte0->paddr)) = 0xdeadbeef;

    result = as_copy(src, &dst);
    KASSERT(result == 0);

    level1 = dst->pages0[15];
    level2 = level1[28];
    level3 = level2[0];
    pte1 = level3[0];
    KASSERT(*((uint32_t *)PADDR_TO_KVADDR(pte1->paddr)) == 0xdeadbeef);

    as_destroy(src);
    as_destroy(dst);
	success(TEST161_SUCCESS, SECRET, "as8");

	return 0;
}

