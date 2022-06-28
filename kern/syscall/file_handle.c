// Methods to operate on file_handle.
//
// Used by the kernel not user mode.

#include <kern/errno.h>
#include <file_handle.h>
#include <lib.h>
#include <limits.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>


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
    KASSERT(fh != NULL);
    kfree(fh->name);
    lock_destroy(fh->file_lock);
    kfree(fh);
}

/*
 * Returns a new file_handle for file path open with flags.
 *
 * Args:
 *   path: String path of file to open.
 *   flags: Mode flags same as open(2).
 *   fh: Pointer to return pointer to new file_handle.
 * 
 * Returns:
 *   0 if successful else errno value.
 */
int
open_file_handle(const char *path, int flags, struct file_handle **fh)
{
    char vfs_path[PATH_MAX];
    struct vnode *vn;
    const mode_t unused_mode = 0x777;
    int result;

    KASSERT(path != NULL);
    KASSERT(strlen(path) < PATH_MAX);
	*fh = create_file_handle(path);
	if (*fh == NULL) {
        return EFAULT;
	}
	// vfs_open destructively uses filepath, so pass in a copy.
	strcpy(vfs_path, path);
	result = vfs_open(vfs_path, flags, unused_mode, &vn);
	if (result) {
        destroy_file_handle(*fh);
        *fh = NULL;
        return result;
	}
    (*fh)->vn = vn;
    (*fh)->flags = flags;
    return 0;
}

/*
 * Closes and destroys file_handle.
 */
void close_file_handle(struct file_handle *fh) 
{
    KASSERT(fh != NULL);
    lock_file_handle(fh);
    KASSERT(fh->ref_count == 0);
    vfs_close(fh->vn);
    release_file_handle(fh);
    destroy_file_handle(fh);
}

/*
 * Lock a file handle.
 */
void lock_file_handle(struct file_handle *fh)
{
    KASSERT(fh != NULL);
    KASSERT(lock_do_i_hold(fh->file_lock) == false);
    lock_acquire(fh->file_lock);
}

/* 
 * Release a file handle.
 */
void release_file_handle(struct file_handle *fh)
{
    KASSERT(fh != NULL);
    lock_release(fh->file_lock);
}