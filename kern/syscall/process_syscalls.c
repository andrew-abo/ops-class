// Kernel facing process system calls.
//
// These functions are meant to be called from syscall.c.  They should
// not be called directly from userspace.

#include <limits.h>
#include <types.h>

#include <syscall.h>
#include <uio.h>
#include <copyinout.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>

// TODO(aabo): Replace with linked list.
//static pid_t next_pid = PID_MIN;
//static proc *processes[128];

/*
 * Spawn a new process.
 *
 * Args:
 *   pid: Parent will see *pid updated to child process.
 *     Child will see *pid == 0.
 * 
 * Returns:
 *   0 on success, else errno.
 */
int sys_fork(pid_t *pid)
{
    struct proc *child;
    struct proc *parent;

    if (pid == NULL) {
        return EFAULT;
    }
    parent = curproc;
    (void)parent;
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }

    return 0;
}