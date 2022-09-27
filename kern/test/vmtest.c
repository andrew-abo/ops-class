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

// Tests get_page_via_table can page in from swapdisk.
int
vmtest6(int nargs, char **args)
{
	vaddr_t vaddr;
	paddr_t paddr;
	int result;
	unsigned i;
	struct pte *pte;
	unsigned page_index = 7;
	vaddr_t faultaddress = 0x10000;
	struct addrspace *as;
	(void)nargs;
	(void)args;

	// Create a test page on disk.
	vaddr = alloc_kpages(1);
	KASSERT(vaddr != 0);
	paddr = KVADDR_TO_PADDR(vaddr);
	// Fill page with known sequence of bytes.
	for (i = 0; i < PAGE_SIZE; i++) {
		*(unsigned char *)(vaddr + i) = i % 256;
	}
	result = block_write(page_index, paddr);
	// Zero out the memory in case we get assigned the same page.
	bzero((void *)vaddr, PAGE_SIZE);
	KASSERT(result == 0);
	free_kpages(vaddr);

	// Create a page table with pte backed by test page.
	as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, faultaddress, 0x2000, 1, 1, 0);
    pte = as_create_page(as, faultaddress);
    KASSERT(pte != NULL);
	pte->status &= ~VM_PTE_VALID;
    free_pages(pte->paddr);
	pte->page_index = page_index;
	pte->status |= VM_PTE_BACKED;

	// Access the backed page via page table.
	result = get_page_via_table(as, faultaddress);
	KASSERT(result == 0);

	// Confirm known sequence read back.
	vaddr = PADDR_TO_KVADDR(pte->paddr);
	for (i = 0; i < PAGE_SIZE; i++) {
		KASSERT(*(unsigned char *)(vaddr + i) == (i % 256));
	}
    as_destroy(as);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm6");
	return 0;
}

// Tests get_page_via_table can create a new page.
int
vmtest7(int nargs, char **args)
{
	int result;
	unsigned used_bytes0;
	vaddr_t faultaddress = 0x10000;
	struct addrspace *as;
	(void)nargs;
	(void)args;

	// Create a page table with pte backed by test page.
	as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, faultaddress, 0x2000, 1, 1, 0);
    
    used_bytes0 = coremap_used_bytes();

	// Access valid but non-existing page to trigger creating new page.
	result = get_page_via_table(as, faultaddress);
	KASSERT(result == 0);

	// Should be at least one more page used, but possibly more for 
	// adding new pte.
	KASSERT(coremap_used_bytes() > used_bytes0);

	// Note: we can't test read/write to the new page because
	// we have not registered it in the TLB, and if we generate
	// a TLB fault, this page is not in the kernel address space.
	
    as_destroy(as);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm7");
	return 0;
}