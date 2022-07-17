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
#include <kern/seek.h>
#include <stat.h>
#include <vfs.h>

/*
 * Checks if file descriptor is in valid range.
 *
 * Args:
 *   fd: File descriptor value.
 * 
 * Returns:
 *   1 if is legal, else 0.
 */
static int
fd_is_legal(int fd)
{
    return (fd >= 0) && (fd < FILES_PER_PROCESS_MAX);
}

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
    if (!fd_is_legal(fd)) {
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
        return EBADF;
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
    if (!fd_is_legal(fd)) {
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
        return EBADF;
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
     size_t filename_len;
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
     fh->ref_count = 1;
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

     return 0;
 }

/*
 * Closes a file descriptor.
 *
 * We need to have the option of not locking the file table for the case
 * where we are closing a file descriptor from another system call when
 * the table lock has already been acquired.
 * 
 * Args:
 *   fd: File descriptor to close.
 *   lock_fd_table: Locks file descriptor table if non-zero, else no locking.
 * 
 * Returns:
 *   0 on success else errno value.  Note vfs_close() does not return a status, 
 *   so we always return 0 unless fd is bad.
 */
int
sys_close(int fd, int lock_fd_table)
{
    struct file_handle *fh;

    if (!fd_is_legal(fd)) {
        return EBADF;
    }
    if (lock_fd_table) {
        lock_acquire(curproc->files_lock);
    }
    fh = curproc->files[fd];
    if (fh == NULL) {
        if (lock_fd_table) {
            lock_release(curproc->files_lock);
        }
        return EBADF;
    }
    curproc->files[fd] = NULL;
    lock_file_handle(fh);
    // Checking ref_count atomically requires duplicating the lock releases
    // in both clauses.
    fh->ref_count--;
    if (fh->ref_count == 0) {
        // We have to assume vfs_close() succeeds because it does not return
        // a status.
        vfs_close(fh->vn);
        release_file_handle(fh);
        destroy_file_handle(fh);
        if (lock_fd_table) {
            lock_release(curproc->files_lock);
        }
        return 0;
    }
    release_file_handle(fh);
    if (lock_fd_table) {
        lock_release(curproc->files_lock);
    }
    return 0;
}

/*
 * Copies a file descriptor.
 *
 * Args:
 *   oldfd: Source file descriptor.
 *   newfd: Destination file descriptor to clobber. Closes if open.
 * 
 * Returns:
 *   0 on success else errno value.
 */
int
sys_dup2(int oldfd, int newfd)
{
    struct file_handle *fh;

    if (!fd_is_legal(oldfd) || !fd_is_legal(newfd)) {
        return EBADF;
    }
    if (oldfd == newfd) {
        return 0;
    }
    lock_acquire(curproc->files_lock);
    if (curproc->files[oldfd] == NULL) {
        lock_release(curproc->files_lock);
        return EBADF;
    }
    if (curproc->files[newfd] != NULL) {
        // Close without locking file descriptor table since we already
        // have the lock and want to keep operations atomic.
        sys_close(newfd, 0);
    }
    curproc->files[newfd] = curproc->files[oldfd];
    fh = curproc->files[newfd];
    lock_file_handle(fh);
    fh->ref_count++;
    release_file_handle(fh);
    lock_release(curproc->files_lock);
    return 0;
}

/*
 * Sets the file descriptor offset for next operation.
 *
 * Args:
 *   fd: File descriptor.
 *   pos: Relative or absolute offset in bytes for next operation.
 *   whence: Enumerated type selects if pos is relative to current
 *     position, from start or end of file.
 * 
 * Returns:
 *   0 on success else errno value.
 */
int
sys_lseek(int fd, off_t pos, int whence, off_t *return_offset) 
{
    int result;
    struct file_handle *fh;
    struct stat statbuf;
    off_t abs_offset = -1;  // Absolute byte position relative to start of file.

    if (!fd_is_legal(fd)) {
        return EBADF;
    }
    lock_acquire(curproc->files_lock);
    fh = curproc->files[fd];
    lock_release(curproc->files_lock);
    if (fh == NULL) {
        return EBADF;
    }
    if (!VOP_ISSEEKABLE(fh->vn)) {
        return ESPIPE;
    }
    result = VOP_STAT(fh->vn, &statbuf);
    if (result) {
        return result;
    }
    switch (whence) {
        case SEEK_SET:
          abs_offset = pos;
          break;
        case SEEK_CUR:
          abs_offset = fh->offset + pos;
          break;
        case SEEK_END:
          abs_offset = statbuf.st_size + pos; 
          break;
        default:
        return EINVAL;
    }
    if (abs_offset < 0) {
        return EINVAL;
    }
    fh->offset = abs_offset;
    *return_offset = abs_offset;
    return 0;
}

/*
 * Gets current working directory for current thread.
 *
 * Args:
 *   buf: Pointer to buffer to return (unterminated) path string.
 *   buflen: Size of buf in bytes.
 *   bytes_in: Pointer to return actual number of bytes read.
 * 
 * Returns:
 *   0 on success, else errno value.
 */
int
sys___getcwd(userptr_t buf, size_t buflen, size_t *bytes_in)
{
    struct iovec iov;
    struct uio my_uio;
    char *kbuf;
    int result;

    KASSERT(bytes_in != NULL);
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }
    uio_kinit(&iov, &my_uio, kbuf, buflen, 0, UIO_READ);
    result = vfs_getcwd(&my_uio);
    if (result) {
        kfree(kbuf);
        return result;
    }
    result = copyout(kbuf, buf, buflen);
    kfree(kbuf);
    if (result) {
        return result;
    }
    *bytes_in = my_uio.uio_offset;
    return 0;
}

/*
 * Sets current working directory for current thread.
 *
 * Args:
 *   pathname: Pointer to path string.
 * 
 * Returns:
 *   0 on success, else errno value.
 */
int
sys_chdir(const_userptr_t pathname)
{
    char kpathname[PATH_MAX];
    size_t pathname_len;
    int result;

    result = copyinstr(pathname, kpathname, PATH_MAX, &pathname_len);
    if (result) {
        return result;
    }
    result = vfs_chdir(kpathname);
    if (result) {
        return result;
    }
    return 0;
}
