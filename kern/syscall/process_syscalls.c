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
static pid_t next_pid = PID_MIN;
static struct proc *processes[128];

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
    //struct thread *parent_thread;
    struct trapframe *tf_copy;
    int result;

    if (pid == NULL) {
        return EINVAL;
    }
    if (tf == NULL) {
        return EINVAL;
    }
    parent = curproc;
    //parent_thread = curthread;
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }
    result = as_copy(parent->p_addrspace, &(child->p_addrspace));
    if (result) {
        return result;
    }
    //child->p_cwd =?
    //next, prev
    copy_file_descriptor_table(child, parent);
    child->ppid = parent->pid;
    tf_copy = copy_trapframe(tf);
    if (tf_copy == NULL) {
        proc_destroy(child);
        return ENOMEM;
    }
    image = stackimage_create();
    if (image == NULL) {
        proc_destroy(child);
        return ENOMEM;
    }
    result = stackimage_save(parent_thread, tf, image);
    if (result) {
        stackimage_destroy(image);
        proc_destroy(child);
        return result;
    }
    // TODO(aabo): replace with a linked list.
    child->pid = next_pid++;
    processes[child->pid] = child;
    // Child returns via enter_forked_process().
    result = thread_fork("fork", child, enter_forked_process, (void *)tf_copy, 0);
    if (result) {
        kfree(tf_copy);
        proc_destroy(child);
        return result;
    }
    //DEBUG
    while (1) {
        ;
    }

    // Parent returns child pid.
    *pid = child->pid;
    return 0;
}