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
#include <kern/wait.h>

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
        proclist_lock_acquire();
        proclist_remove(child->pid);
        proclist_lock_release();
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

/*
 * Exits the current process.
 *
 * Args:
 *   exitcode: User supplied exit code.
 */
void
sys__exit(int exitcode)
{
    struct proc *proc;

    curproc->exit_status = _MKWAIT_EXIT(exitcode);
    proclist_reparent(curproc->pid);
	for (int fd = 0; fd < FILES_PER_PROCESS_MAX; fd++) {
        if (curproc->files[fd] != NULL) {
            // sys_close() refers to curproc global, which
            // requires thread to still be attached to this
            // process, so this must be done before proc_remthread().
            sys_close(fd, 0);
        }
    }
    // curproc becomes NULL once we call proc_remthread, so save it.
    proc = curproc;
	proc_remthread(curthread);
    proc_zombify(proc);
    lock_acquire(proc->waitpid_lock);
    cv_broadcast(proc->waitpid_cv, proc->waitpid_lock);
    lock_release(proc->waitpid_lock);
    thread_exit();
}