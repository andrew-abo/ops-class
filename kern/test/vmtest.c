/*
 * Virtual memory test code.
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <kern/test161.h>
#include <synch.h>
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
	int swap_enabled;

	swap_enabled = set_swap_enabled(0);
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
	set_swap_enabled(swap_enabled);

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
	int swap_enabled;

	swap_enabled = set_swap_enabled(0);
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
	set_swap_enabled(swap_enabled);

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
	int swap_enabled;

	swap_enabled = set_swap_enabled(0);
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
	set_swap_enabled(swap_enabled);

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
	int swap_enabled;

	swap_enabled = set_swap_enabled(0);
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
	set_swap_enabled(swap_enabled);

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
	vaddr_t faultaddress = 0x10000;
	struct addrspace *as;
	(void)nargs;
	(void)args;

	as = as_create();
    KASSERT(as != NULL);
    as_define_region(as, faultaddress, 0x2000, 1, 1, 0);
    pte = create_test_page(as, faultaddress);
    KASSERT(pte != NULL);

	paddr = pte->paddr;
	vaddr = PADDR_TO_KVADDR(paddr);
	// Fill page with known sequence of bytes.
	for (i = 0; i < PAGE_SIZE; i++) {
		*(unsigned char *)(vaddr + i) = i % 256;
	}

	// Swap page out.
	result = maybe_swap_out(pte, /*dirty=*/1);
	KASSERT(result == 0);
	free_pages(paddr);
	
	// Zero out the old memory for good meeasure.
	bzero((void *)vaddr, PAGE_SIZE);

	// Access the backed page via page table.
	result = get_page_via_table(as, faultaddress);
	KASSERT(result == 0);
	vaddr = PADDR_TO_KVADDR(pte->paddr);

	// Confirm known sequence read back.
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

	// Create a page table with no pages.
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

// Tests a victim page is properly selected.
int
vmtest8(int nargs, char **args)
{
	struct addrspace *as;
	paddr_t paddr;
	unsigned p;
	unsigned pages = 1000;
	struct pte *pte;
	(void)nargs;
	(void)args;
	int swap_enabled;

	swap_enabled = set_swap_enabled(0);

	// Create a test address space with some user pages.
	as = as_create();
    KASSERT(as != NULL);

    // Exhaust memory.
    for (p = 0; p < pages; p++) {
        pte = create_test_page(as, 0x1000 * p);
		if (pte == NULL) {
			break;
		}
	}

	// Invoke eviction policy.
	paddr = locking_find_victim_page();
	kprintf("victim paddr = 0x%08x\n", paddr);
	KASSERT(paddr > 0);
    as_destroy(as);
	set_swap_enabled(swap_enabled);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm8");
	return 0;
}

// Tests a victim page is properly evicted.
int
vmtest9(int nargs, char **args)
{
	struct addrspace *as;
	vaddr_t kvaddr;
	vaddr_t vaddr;
	paddr_t paddr;
	unsigned p;
	struct pte *pte;
	(void)nargs;
	(void)args;
	int result;
	unsigned core_idx;
	vaddr_t *core_idx_to_vaddr;
	int old_swap_enabled;
	// Choose a number large enough to exhaust memory when
	// swap is disabled.
	unsigned test_pages = 4096;

	// Disable swapping while we exhaust memory.
	old_swap_enabled = set_swap_enabled(0);
	KASSERT(old_swap_enabled);

	// Allocate large array dynamically so we don't overflow
	// the small kernel stack.
	core_idx_to_vaddr = kmalloc(sizeof(vaddr_t) * test_pages);
	KASSERT(core_idx_to_vaddr != NULL);

	// Create a test address space with some user pages.
	as = as_create();
    KASSERT(as != NULL);

    // Exhaust memory.
    for (p = 0; p < test_pages; p++) {
		vaddr = 0x1000 * p;
        pte = create_test_page(as, vaddr);
		if (pte == NULL) {
			break;
		}
		// Write a unique test pattern to each page.
		*(unsigned *)PADDR_TO_KVADDR(pte->paddr) = p;
		core_idx = paddr_to_core_idx(pte->paddr);
		core_idx_to_vaddr[core_idx] = vaddr;
	}
	KASSERT(pte == NULL);

	// Re-enable swapping.
	set_swap_enabled(1);

	// Evict a page.
	result = evict_page(&core_idx);
	KASSERT(result == 0);
	paddr = core_idx_to_paddr(core_idx);
	kvaddr = PADDR_TO_KVADDR(paddr);

	// Zero out the page in memory.
	bzero((void *)kvaddr, PAGE_SIZE);

	// Page-in from swapdisk.
	vaddr = core_idx_to_vaddr[core_idx];
	lock_acquire(as->pages_lock);
	pte = as_lookup_pte(as, vaddr);
	lock_release(as->pages_lock);
	KASSERT(pte != NULL);
	block_read(pte->block_index, paddr);
	// Check unique test pattern has been restored.
	KASSERT(*(unsigned *)kvaddr == vaddr / 0x1000);

	free_pages(paddr);
    as_destroy(as);
	kfree(core_idx_to_vaddr);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm9");
	return 0;
}

// Tests we can read/write more pages than are in physical memory.
//
// Note: test with ramsize=1M to ensure we run cause page outs.
//
int
vmtest10(int nargs, char **args)
{
	struct addrspace *as;
	vaddr_t vaddr;
	unsigned p;
	struct pte *pte;
	(void)nargs;
	(void)args;
	size_t swap0, swap1;
	size_t mem0, mem1;
	// Choose a number that is large enough to cause some page evictions.
	unsigned test_pages = 4096;

	mem0 = coremap_used_bytes();
	swap0 = swap_used_pages();

	// Create a test address space with some user pages.
	as = as_create();
    KASSERT(as != NULL);

    // Exhaust memory to cause some page evictions.
    for (p = 0; p < test_pages; p++) {
		vaddr = 0x1000 * p;
        pte = create_test_page(as, vaddr);
		KASSERT(pte != NULL);
	}

	// Clean up.
    as_destroy(as);
	mem1 = coremap_used_bytes();
	swap1 = swap_used_pages();
	KASSERT(swap0 == swap1);
	KASSERT(mem0 == mem1);

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "vm10");
	return 0;
}
