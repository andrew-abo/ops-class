// Kernel facing process system calls.
//
// These functions are meant to be called from syscall.c.  They should
// not be called directly from userspace.

#include <limits.h>
#include <types.h>

#include <addrspace.h>
#include <syscall.h>
#include <uio.h>
#include <copyinout.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>

// TODO(aabo): Replace with linked list.
//static pid_t next_pid = PID_MIN;
//static struct proc *processes[128];

/*
 * Spawn a new process.
 *
 * Args:
 *   pid: Parent will see *pid updated to child process.
 *     Child will see *pid == 0.
 *   tf: Pointer to trapframe of parent process.
 * 
 * Returns:
 *   0 on success, else errno.
 */
int sys_fork(pid_t *pid, struct trapframe *tf)
{
    struct proc *child;
    struct proc *parent;
    struct stackimage *image;
    int result;

    if (pid == NULL) {
        return EINVAL;
    }
    if (tf == NULL) {
        return EINVAL;
    }
    parent = curproc;
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }
    result = as_copy(parent->p_addrspace, &child->p_addrspace);
    if (result) {
        return result;
    }
    //child->p_cwd =?
    //pid
    //ppid
    //next, prev
    copy_file_descriptor_table(child, parent);
    child->ppid = parent->pid;
    image = stackimage_create();
    if (image == NULL) {
        return ENOMEM;
    }
    return 0;
}