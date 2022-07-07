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


void
enter_forked_process()
{
	// TODO(aabo): copy parent stack image to child.
	// modify trapframe to return pid=0
    // thread_fork(entrypoint=enter_forked_process)
	mips_usermode(tf);
}

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
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }

    return 0;
}