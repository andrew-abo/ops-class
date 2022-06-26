// Kernel facing file I/O system calls.

#include <limits.h>
#include <types.h>

#include <syscall.h>
#include <uio.h>
#include <copyinout.h>
#include <current.h>
#include <file_handle.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/iovec.h>

/*
 * Validates arguments and passes on to VOP_WRITE.
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

    if (bytes_out == NULL) {
        return EFAULT;
    }
    *bytes_out = 0;
    if ((fd < 0) || (fd > FILES_PER_PROCESS_MAX)) {
        return EINVAL;
    }
    fh = curproc->files[fd];
    if (fh == NULL) {
        return EINVAL;
    }
    kbuf = (char *)kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }
    uio_kinit(&iov, &my_uio, kbuf, buflen, 0, UIO_WRITE);
    result = copyin(buf, kbuf, buflen);
    if (result) {
        kfree(kbuf);
        return result;
    }
    result = VOP_WRITE(fh->vn, &my_uio);
    kfree(kbuf);
    if (result) {
        return result;
    }
    *bytes_out = buflen - my_uio.uio_resid;
    return 0;
}
