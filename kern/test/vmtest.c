/*
 * Virtual memory test code.
 *
 */

#include <types.h>
#include <lib.h>
//#include <clock.h>
//#include <thread.h>
//#include <proc.h>
#include <test.h>
#include <kern/test161.h>
//#include <spinlock.h>
#include <vm.h>

#define BLOCKS 32768

// Tests core pages can be allocated and freed.
int
vmtest1(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	unsigned used_bytes0;
	unsigned used_bytes1;
	paddr_t paddr[BLOCKS];
	int i;
	unsigned block_size;

	used_bytes0 = coremap_used_bytes();

	// Allocate all memory in blocks of uniform sizes.
	for (block_size = 1; block_size < 16; block_size++) {
        for (i = 0; i < BLOCKS; i++) {
            paddr[i] = alloc_pages(block_size, NULL, (vaddr_t)NULL);
            if (paddr[i] == (paddr_t)NULL) {
                break;
            }
        }
        used_bytes1 = coremap_used_bytes();
        KASSERT(used_bytes1 - used_bytes0 == (unsigned) i * block_size * PAGE_SIZE);
        i--;
        kprintf("last page allocated: paddr[%d] = 0x%08x\n", i, paddr[i]);

        // Free in descending order.
        for (int j = i; j >= 0; j--) {
            free_pages(paddr[j]);
        }
        KASSERT(coremap_used_bytes() == used_bytes0);
    }
	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm1");
	return 0;
}

int
vmtest2(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	unsigned used_bytes0;
	unsigned used_bytes1;
	paddr_t paddr[BLOCKS];
	int i;
	unsigned block_size;

	used_bytes0 = coremap_used_bytes();

	// Allocate all memory in blocks of uniform sizes.
	for (block_size = 1; block_size < 16; block_size++) {
        for (i = 0; i < BLOCKS; i++) {
            paddr[i] = alloc_pages(block_size, NULL, (vaddr_t)NULL);
            if (paddr[i] == (paddr_t)NULL) {
                break;
            }
        }
        used_bytes1 = coremap_used_bytes();
        KASSERT(used_bytes1 - used_bytes0 == (unsigned) i * block_size * PAGE_SIZE);
        i--;
        kprintf("last page allocated: paddr[%d] = 0x%08x\n", i, paddr[i]);

        // Free in ascending order.
        for (int j = 0; j <= i; j++) {
            free_pages(paddr[j]);
        }
        KASSERT(coremap_used_bytes() == used_bytes0);
    }
	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm2");
	return 0;
}

int
vmtest3(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	unsigned used_bytes0;
	unsigned used_bytes1;
	paddr_t paddr[BLOCKS];
	int i;
	unsigned block_size;
	unsigned my_used_pages;

	used_bytes0 = coremap_used_bytes();

    // Allocate all memory in blocks of random sizes up to 32.
    my_used_pages = 0;
    for (i = 0; i < BLOCKS; i++) {
		block_size = random() % 32 + 1;
		paddr[i] = alloc_pages(block_size, NULL, (vaddr_t)NULL);
		if (paddr[i] == (paddr_t)NULL) {
			break;
		}
		my_used_pages += block_size;
	}
	used_bytes1 = coremap_used_bytes();
	KASSERT(used_bytes1 - used_bytes0 == my_used_pages * PAGE_SIZE);
	i--;
	kprintf("last page allocated: paddr[%d] = 0x%08x\n", i, paddr[i]);

	// Free in descending order.
	for (int j = i; j >= 0; j--) {
		free_pages(paddr[j]);
    }
    KASSERT(coremap_used_bytes() == used_bytes0);


	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm3");
	return 0;
}

int
vmtest4(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	unsigned used_bytes0;
	unsigned used_bytes1;
	paddr_t paddr[BLOCKS];
	int i;
	unsigned block_size;
	unsigned my_used_pages;

	used_bytes0 = coremap_used_bytes();

    // Allocate all memory in blocks of random sizes up to 32.
    my_used_pages = 0;
    for (i = 0; i < BLOCKS; i++) {
		block_size = random() % 32 + 1;
		paddr[i] = alloc_pages(block_size, NULL, (vaddr_t)NULL);
		if (paddr[i] == (paddr_t)NULL) {
			break;
		}
		my_used_pages += block_size;
	}
	used_bytes1 = coremap_used_bytes();
	KASSERT(used_bytes1 - used_bytes0 == my_used_pages * PAGE_SIZE);
	i--;
	kprintf("last page allocated: paddr[%d] = 0x%08x\n", i, paddr[i]);

	// Free in ascending order.
	for (int j = 0; j <= i; j++) {
		free_pages(paddr[j]);
    }
    KASSERT(coremap_used_bytes() == used_bytes0);


	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm4");
	return 0;
}
