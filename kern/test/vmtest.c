/*
 * Virtual memory test code.
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <kern/test161.h>
#include <vm.h>

// CAUTION: if local array exceeds PAGE_SIZE bytes the kernel stack
// will overflow into the kernel code segment.
#define BLOCKS 512

// Tests core pages can be allocated and freed in descending order.
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
				kprintf("Attempt to exhaust memory successful.\n");
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

        // Should be at least one free page now.
		paddr[0] = alloc_pages(1, NULL, (vaddr_t)NULL);
		KASSERT(paddr[0] != (paddr_t)NULL);
		free_pages(paddr[0]);

        KASSERT(coremap_used_bytes() == used_bytes0);
    }
	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm1");
	return 0;
}

// Tests core pages can be allocated and freed in ascending order.
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
				kprintf("Attempt to exhaust memory successful.\n");
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
        // Should be at least one free page now.
		paddr[0] = alloc_pages(1, NULL, (vaddr_t)NULL);
		KASSERT(paddr[0] != (paddr_t)NULL);
		free_pages(paddr[0]);

        KASSERT(coremap_used_bytes() == used_bytes0);
    }
	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm2");
	return 0;
}

// Tests blocks can be allocated in random sizes and freed in descending order.
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

    // Should be at least one free page now.
    paddr[0] = alloc_pages(1, NULL, (vaddr_t)NULL);
    KASSERT(paddr[0] != (paddr_t)NULL);
    free_pages(paddr[0]);
    
    KASSERT(coremap_used_bytes() == used_bytes0);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm3");
	return 0;
}

// Tests blocks can be allocated in random sizes and freed in ascending order.
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

    // Should be at least one free page now.
    paddr[0] = alloc_pages(1, NULL, (vaddr_t)NULL);
    KASSERT(paddr[0] != (paddr_t)NULL);
    free_pages(paddr[0]);

    KASSERT(coremap_used_bytes() == used_bytes0);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm4");
	return 0;
}

// Tests if swap blocks can be written and read.
int
vmtest5(int nargs, char **args)
{
	vaddr_t vaddr;
	paddr_t paddr;
	int result;
	unsigned i;
	(void)nargs;
	(void)args;

	vaddr = alloc_kpages(1);
	KASSERT(vaddr != 0);
	paddr = KVADDR_TO_PADDR(vaddr);

	// Fill page with known sequence of bytes.
	for (i = 0; i < PAGE_SIZE; i++) {
		*(unsigned char *)(vaddr + i) = i % 256;
	}
	result = block_write(0, paddr);
	KASSERT(result == 0);
	bzero((void *)vaddr, PAGE_SIZE);
	result = block_read(0, paddr);
	KASSERT(result == 0);
	
	// Confirm known sequence read back.
	for (i = 0; i < PAGE_SIZE; i++) {
		KASSERT(*(unsigned char *)(vaddr + i) == (i % 256));
	}
	free_kpages(vaddr);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm5");
	return 0;
}