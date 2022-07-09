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

/*
 * Returns a heap allocated copy of trapframe.
 *
 * Args:
 *   tf: Pointer to trapframe to copy.
 * 
 * Returns;
 *   Pointer to new copy if successful, else NULL
 */
static struct trapframe
*copy_trapframe(struct trapframe *tf)
{
    struct trapframe *tf_copy;

    KASSERT(tf != NULL);
    tf_copy = kmalloc(sizeof(struct trapframe));
    if (tf_copy == NULL) {
        return NULL;
    }
    memcpy((void *)tf_copy, (void *)tf, sizeof(struct trapframe));
    return tf_copy;
}

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
    struct trapframe *tf_copy;
    int result;

    KASSERT(pid != NULL);
    KASSERT(tf != NULL);
    parent = curproc;
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }
    result = as_copy(parent->p_addrspace, &(child->p_addrspace));
    if (result) {
        return result;
    }

    lock_acquire(parent->files_lock);
    copy_file_descriptor_table(child, parent);
    lock_release(parent->files_lock);

    spinlock_acquire(&parent->p_lock);
    if (parent->p_cwd != NULL) {
		VOP_INCREF(parent->p_cwd);
		child->p_cwd = parent->p_cwd;
	}
    child->ppid = parent->pid;
	spinlock_release(&parent->p_lock);

    tf_copy = copy_trapframe(tf);
    if (tf_copy == NULL) {
        proc_destroy(child);
        return ENOMEM;
    }
    result = trapframe_save(&tf_copy, tf);
    if (result) {
        proc_destroy(child);
        return result;
    }

    proclist_lock_acquire();
    proclist_insert(child);
    proclist_lock_release();

    // Child returns via enter_forked_process().
    result = thread_fork("fork", child, enter_forked_process, (void *)tf_copy, 0);
    if (result) {
        kfree(tf_copy);
        proc_destroy(child);
        return result;
    }
    // Parent returns child pid.
    *pid = child->pid;
    return 0;
}

/*
 * Gets current process ID.
 *
 * Args:
 *   pid: Pointer to return process ID.
 * 
 * Returns
 *   0 always.
 */
int sys_getpid(pid_t *pid)
{
    KASSERT(pid != NULL);
    *pid = curproc->pid;
    return 0;
}
