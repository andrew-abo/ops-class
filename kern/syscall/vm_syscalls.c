// Virutal memory related system calls.

#include <types.h>
#include <kern/errno.h>
#include <addrspace.h>
#include <proc.h>
#include <synch.h>

/*
 * Increments the heap break by amount bytes.
 *
 * Args:
 *   amount: Number of bytes to increase/decrease the heap break if 
 *     positive/negative.
 *   mem: Pointer to return the previous break.
 * 
 * Returns:
 *   0 on success, else errno.
 */
int sys_sbrk(intptr_t amount, void **mem)
{
	struct addrspace *as;
	long unsigned abs_amount;
	vaddr_t vaddr;
	intptr_t newheaptop;

	// For simplicity we require amount be an integer number of pages.
	abs_amount = amount >= 0 ? amount : -amount;
	if ((abs_amount / PAGE_SIZE) * PAGE_SIZE != abs_amount) {
		return EINVAL;
	}
	as = proc_getas();
	if (as == NULL) {
		return EFAULT;
	}
	/* Assert that the address space is not empty. */
	KASSERT(as->next_segment != 0);
	lock_acquire(as->heap_lock);
	KASSERT(as->vheapbase > 0);
	KASSERT(as->vheaptop >= as->vheapbase);
	*mem = (void *)(as->vheaptop);
	newheaptop = (long int)(as->vheaptop) + amount;
	if (amount >= 0) {
		if ((newheaptop - (intptr_t)(as->vheapbase)) / PAGE_SIZE > USER_HEAP_PAGES) {
            lock_release(as->heap_lock);
			return ENOMEM;
		}
		as->vheaptop = (vaddr_t)newheaptop;
		lock_release(as->heap_lock);
		return 0;
	}
	// Negative amount (freeing memory).
	if (newheaptop < (intptr_t)(as->vheapbase)) {
		lock_release(as->heap_lock);
		return EINVAL;
	}
	for (vaddr = (vaddr_t)newheaptop; vaddr < as->vheaptop; vaddr += PAGE_SIZE) {
		as_destroy_page(as, vaddr);
	}
	as->vheaptop = (vaddr_t)newheaptop;
	lock_release(as->heap_lock);
	return 0;
}