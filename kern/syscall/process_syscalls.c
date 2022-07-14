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
#include <synch.h>

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
    spinlock_acquire(&curproc->p_lock);
    *pid = curproc->pid;
    spinlock_release(&curproc->p_lock);
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

    // curproc becomes NULL once we call proc_remthread, so save it.
    proc = curproc;

    spinlock_acquire(&proc->p_lock);
    proc->exit_status = _MKWAIT_EXIT(exitcode);
    spinlock_release(&proc->p_lock);

    proclist_lock_acquire();    
    proclist_reparent(proc->pid);
    proclist_lock_release();

    lock_acquire(proc->files_lock);
	for (int fd = 0; fd < FILES_PER_PROCESS_MAX; fd++) {
        if (proc->files[fd] != NULL) {
            // sys_close() refers to curproc global, which
            // requires thread to still be attached to this
            // process, so this must be done before proc_remthread().
            sys_close(fd, 0);
        }
    }
    lock_release(proc->files_lock);

	proc_remthread(curthread);
    proc_zombify(proc);
    lock_acquire(proc->waitpid_lock);
    cv_broadcast(proc->waitpid_cv, proc->waitpid_lock);
    lock_release(proc->waitpid_lock);
    thread_exit();
}

/*
 * Waits for specified process ID to exit.
 * 
 * Args:
 *   pid: Process ID to wait for.
 *   status: Optional pointer to return process exit status.
 *     NULL ignores exit status.
 *   options: Option flags, see waitpid(2). Limited support.
 * 
 * Returns:
 *   0 on success, else errno.
 */
int sys_waitpid(pid_t pid, userptr_t status, int options)
{
    struct proc *child;
    int child_status;
    int result;

    if (options != 0) {
        return EINVAL;
    }
    proclist_lock_acquire();
    child = proclist_lookup(pid);
    proclist_lock_release();

    if (child == NULL) {
        return ESRCH;
    }

    spinlock_acquire(&child->p_lock);
    spinlock_acquire(&curproc->p_lock);
    if (child->ppid != curproc->pid) {
        spinlock_release(&curproc->p_lock);
        spinlock_release(&child->p_lock);
        return ECHILD;
    }
    spinlock_release(&curproc->p_lock);
    if (child->p_state != S_ZOMBIE) {
        spinlock_release(&child->p_lock);
        lock_acquire(child->waitpid_lock);
        cv_wait(child->waitpid_cv, child->waitpid_lock);
        lock_release(child->waitpid_lock);
        spinlock_acquire(&child->p_lock);
    }
    child_status = child->exit_status;
    spinlock_release(&child->p_lock);

    proclist_lock_acquire();
    // TODO(aabo): Looking through the linked list above and here is
    // inefficient.
    child = proclist_remove(pid);
    proclist_lock_release();

    proc_destroy(child);
    if (status != NULL) {
        result = copyout(&child_status, status, sizeof(int));
        if (result) {
            return result;
        }
    }
    return 0;
}

// Initial number of max elements in pointer list.
#define string_list_INITIAL_MAX 2

// Max number of elements in pointer list after resizing.
#define string_list_MAX_MAX 512

// An expandable array of pointers.
struct string_list {
    size_t used;  // Number of non-empty elements in list.
    size_t max;  // Max number of elements in list before resize. 
    char **list;  // Array of pointers.
};

/*
 * Allocates a new pointer list.
 *
 * Returns:
 *   Pointer to new list if successful, else NULL.
 */
static struct string_list 
*string_list_create() {
    struct string_list *plist;

    plist = kmalloc(sizeof(struct string_list));
    if (plist == NULL) {
        return NULL;
    }
    plist->used = 0;
    plist->max = string_list_INITIAL_MAX;
    plist->list = kmalloc(sizeof(char *) * (plist->max));
    if (plist->list == NULL) {
        kfree(plist);
        return NULL;
    }
    return plist;
}

static void
string_list_destroy(struct string_list *plist)
{
    KASSERT(plist != NULL);
    kfree(plist->list);
    kfree(plist);
}

/*
 * Appends a new element at end of list.
 *
 * Args:
 *   plist: Pointer to list to append to.
 *   value: Value of element to append.
 * 
 * Returns:
 *   index of element appended if successful else -1.\
 */
static int 
string_list_append(struct string_list *plist, char *value)
{
    char **new;
    size_t new_max;

    KASSERT(plist != NULL);
    KASSERT(plist->used <= plist->max);
    if (plist->used == plist->max) {
        new_max = (plist->max) << 1;
        if (new_max > string_list_MAX_MAX) {
            return -1;
        }
        new = kmalloc(sizeof(char *) * new_max);
        if (new == NULL) {
            return -1;
        }
        memcpy(new, plist->list, (plist->used) * sizeof(char *));
        kfree(plist->list);
        plist->list = new;
        plist->max = new_max;
    }
    plist->list[plist->used] = value;
    return plist->used++;
}

/*
 * Replace current process with executable from filesystem.
 *
 * Args:
 *   progname: String of executable path.
 *   args: Arguments to pass to progname.  List of strings, each NULL terminated,
 *     with last string pointing to NULL.
 * 
 * Returns:
 *   errno if error else does not return.
 */
int sys_execv(userptr_t progname, userptr_t args)
{
    (void)progname;
    (void)args;
    struct string_list *plist;
    const char *a = "a";
    const char *b = "b";
    const char *c = "c";

    plist = string_list_create();
    KASSERT(plist != NULL);

    int result;
    for (int i = 0; i < 515; i++) {
        result = string_list_append(plist, (char *)a);
        kprintf("%d: %d\n", i, result);
    }
    string_list_append(plist, (char *)a);
    string_list_append(plist, (char *)b);
    string_list_append(plist, (char *)c);
    string_list_append(plist, (char *)a);
    string_list_append(plist, (char *)b);
    string_list_append(plist, (char *)c);

    kprintf("hello\n");

    for (int i = 0; i < 6; i++) {
        kprintf("plist->list[%d] = %s\n", i, plist->list[i]);
    }
    kprintf("plist->used = %d\n", plist->used);
    kprintf("plist->max = %d\n", plist->max);

    string_list_destroy(plist);
    return 0;
}

/*
 * Debugging aid.
 *
 * This is not standard.  We hijack the getlogin syscall 
 * so we can run things in the kernel.  It is a custom debugging aid.
 */
void
sys___getlogin()
{
    proclist_lock_acquire();
    proclist_print();
    proclist_lock_release();
}