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
    fh = curproc->files[fd];
    if (fh == NULL) {
        return EBADF;
    }
    lock_file_handle(fh);
    access = fh->flags & O_ACCMODE;
    if (access == O_RDONLY) {
        release_file_handle(fh);
        return EACCES;
    }
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        release_file_handle(fh);
        return ENOMEM;
    }
    uio_kinit(&iov, &my_uio, kbuf, buflen, fh->offset, UIO_WRITE);
    result = copyin(buf, kbuf, buflen);
    if (result) {
        kfree(kbuf);
        release_file_handle(fh);
        return result;
    }
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
    fh = curproc->files[fd];
    if (fh == NULL) {
        return EBADF;
    }
    lock_file_handle(fh);
    access = fh->flags & O_ACCMODE;
    if (access == O_WRONLY) {
        release_file_handle(fh);
        return EACCES;
    }
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        release_file_handle(fh);
        return ENOMEM;
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
 * Opens a file.
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
     *fd = new_file_descriptor();
     if (*fd < 0) {
         destroy_file_handle(fh);
         return EMFILE;
     }
     curproc->files[*fd] = fh;
     fh->ref_count = 1;
     fh->flags = flags;
     return 0;
 }
