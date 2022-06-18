// Methods to operate on file_handle.
//
// Used by the kernel not user mode.

#include <file_handle.h>
#include <lib.h>
#include <synch.h>


/*
 * Create (allocate) a new file_handle in kernel address space.
 *
 * Args:
 *   name: String identifier for file_handle.
 * 
 * Returns:
 *   Pointer to new file_handle, else NULL if failed.
 */
struct file_handle
*create_file_handle(const char *name) 
{
    struct file_handle *fh;

    KASSERT(name != NULL);

    fh = kmalloc(sizeof(struct file_handle));
    if (fh == NULL) {
        return NULL;
    }
    fh->name = kstrdup(name);
    if (fh->name == NULL) {
        kfree(fh);
        return NULL;
    }
    fh->offset = 0;
    fh->vn = NULL;
    fh->file_lock = lock_create(name);
    if (fh->file_lock == NULL) {
        kfree(fh->name);
        kfree(fh);
        return NULL;
    }
    fh->ref_count = 0;
    fh->flags = 0x0;
    return fh;
}

/*
 * Destroy file_handle fh.
 *
 * Args:
 *   fh: Pointer to file_handle to destroy.
 */
void
destroy_file_handle(struct file_handle *fh)
{
    struct lock *file_lock;
 
    KASSERT(fh != NULL);
    KASSERT(lock_do_i_hold(fh->file_lock) == false);

    lock_acquire(fh->file_lock);
    KASSERT(fh->ref_count == 0);
    KASSERT(fh->file_lock != NULL);
    file_lock = fh->file_lock;
    kfree(fh->name);
    kfree(fh);
    lock_release(file_lock);
    lock_destroy(file_lock);
}
