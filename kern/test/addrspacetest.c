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

