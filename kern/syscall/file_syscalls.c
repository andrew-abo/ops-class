// Kernel facing file I/O system calls.
//
// These functions are meant to be called from syscall.c.  They should
// not be called directly from userspace.

#include <limits.h>
#include <types.h>

#include <syscall.h>
#include <uio.h>
#include <copyinout.h>
#include <current.h>
#include <file_handle.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/iovec.h>
#include <stat.h>
#include <vfs.h>

/*
 * Write to a file descriptor.
 *
 * Implements the process level write() system call.
 *
 * Args:
 *   fd: File descriptor to write to.
 *   buf: Pointer to start of userspace byte buffer.
 *   buflen: Number of bytes to write.
 *   bytes_out: Pointer to int to return actual number of bytes written.
 * 
 * Returns:
 *   0 if successful, else errno value.
 */
int 
sys_write(int fd, const userptr_t buf, size_t buflen, size_t *bytes_out)
{
    struct iovec iov;
    struct uio my_uio;
    char *kbuf;
    struct file_handle *fh;
    int result;
    int access;

    KASSERT(bytes_out != NULL);
    *bytes_out = 0;
    if ((fd < 0) || (fd > FILES_PER_PROCESS_MAX)) {
        return EBADF;
    }
    lock_acquire(curproc->files_lock);
    fh = curproc->files[fd];
    lock_release(curproc->files_lock);
    if (fh == NULL) {
        return EBADF;
    }
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }
    result = copyin(buf, kbuf, buflen);
    if (result) {
        kfree(kbuf);
        return result;
    }
    lock_file_handle(fh);
    access = fh->flags & O_ACCMODE;
    if (access == O_RDONLY) {
        kfree(kbuf);
        release_file_handle(fh);
        return EACCES;
    }
    uio_kinit(&iov, &my_uio, kbuf, buflen, fh->offset, UIO_WRITE);
    result = VOP_WRITE(fh->vn, &my_uio);
    kfree(kbuf);
    if (result) {
        release_file_handle(fh);
        return result;
    }
    *bytes_out = buflen - my_uio.uio_resid;
    fh->offset += *bytes_out;
    release_file_handle(fh);
    return 0;
}

/*
 * Reads from a file descriptor.
 *
 * Implements the process level read() system call.
 *
 * Args:
 *   fd: File descriptor to write to.
 *   buf: Pointer to start of userspace byte buffer.
 *   buflen: Number of bytes to attempt to read.
 *   bytes_in: Pointer to int to return actual number of bytes read.
 * 
 * Returns:
 *   0 if successful, else errno value.
 */
int 
sys_read(int fd, userptr_t buf, size_t buflen, size_t *bytes_in)
{
    struct iovec iov;
    struct uio my_uio;
    char *kbuf;
    struct file_handle *fh;
    int result;
    int access;

    KASSERT(bytes_in != NULL);
    *bytes_in = 0;
    if ((fd < 0) || (fd > FILES_PER_PROCESS_MAX)) {
        return EBADF;
    }
    lock_acquire(curproc->files_lock);
    fh = curproc->files[fd];
    lock_release(curproc->files_lock);
    if (fh == NULL) {
        return EBADF;
    }
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }
    lock_file_handle(fh);
    access = fh->flags & O_ACCMODE;
    if (access == O_WRONLY) {
        kfree(kbuf);
        release_file_handle(fh);
        return EACCES;
    }
    uio_kinit(&iov, &my_uio, kbuf, buflen, fh->offset, UIO_READ);
    result = VOP_READ(fh->vn, &my_uio);
    if (result) {
        kfree(kbuf);
        release_file_handle(fh);
        return result;
    }
    result = copyout(kbuf, buf, buflen);
    kfree(kbuf);
    if (result) {
        release_file_handle(fh);
        return result;
    }
    *bytes_in = buflen - my_uio.uio_resid;
    fh->offset += *bytes_in;
    release_file_handle(fh);
    return 0;
}

/*
 * Returns a new file descriptor.
 *
 * Returns:
 *   Non-negative integer if successful else -1.
 */
static int
new_file_descriptor()
{
    int fd;

    // Reserve 0, 1, 2 for STDIN, STDOUT, STDERR.
    for (fd = 3; fd < FILES_PER_PROCESS_MAX; fd++) {
        if (curproc->files[fd] == NULL) {
            return fd;
        }
    }
    return -1;
}

/*
 * Opens a file descriptor.
 *
 * Implements the process level open() system call.
 * 
 * Args:
 *   filename: Path to file to open.
 *   flags: fcntl.h access flags.
 *   fd: Pointer to return file descriptor.
 * 
 * Returns:
 *   0 if successful, else errno value.
 */
 int
 sys_open(const_userptr_t filename, int flags, int *fd)
 {
     char kfilename[PATH_MAX];
     unsigned filename_len;
     struct file_handle *fh;
     struct stat statbuf;
     int result;

     KASSERT(fd != NULL);
     result = copyinstr(filename, kfilename, PATH_MAX, &filename_len);
     if (result) {
         return result;
     }
     if (filename_len == PATH_MAX) {
         return ENAMETOOLONG;
     }
     result = open_file_handle(kfilename, flags, &fh);
     if (result) {
         return result;
     }
     if (flags & O_APPEND) {
         result = VOP_STAT(fh->vn, &statbuf);
         if (result) {
             destroy_file_handle(fh);
             return result;
         }
         fh->offset = statbuf.st_size;
     }
     lock_acquire(curproc->files_lock);
     *fd = new_file_descriptor();
     if (*fd < 0) {
         lock_release(curproc->files_lock);
         destroy_file_handle(fh);
         return EMFILE;
     }
     curproc->files[*fd] = fh;
     lock_release(curproc->files_lock);
     fh->ref_count = 1;
     fh->flags = flags;
     return 0;
 }

/*
 * Closes a file descriptor.
 *
 * Args:
 *   fd: File descriptor to close.
 * 
 * Returns:
 *   0 on success else errno value.  Note vfs_close() does not return a status, 
 *   so we always return 0 unless fd is bad.
 */
int
sys_close(int fd)
{
    struct file_handle *fh;

    lock_acquire(curproc->files_lock);
    fh = curproc->files[fd];
    if (fh == NULL) {
        lock_release(curproc->files_lock);
        return EBADF;
    }
    lock_file_handle(fh);
    // We have to assume vfs_close() succeeds because it does not return
    // a status.
    vfs_close(fh->vn);
    // Checking ref_count atomically requires duplicating the lock releases
    // in both clauses.
    if(--fh->ref_count == 0) {
        release_file_handle(fh);
        destroy_file_handle(fh);
        curproc->files[fd] = NULL;
        lock_release(curproc->files_lock);
        return 0;
    }
    release_file_handle(fh);
    lock_release(curproc->files_lock);
    return 0;
}