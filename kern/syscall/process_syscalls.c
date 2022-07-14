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
    int used;  // Number of non-empty elements in list.
    int max;  // Max number of elements in list before resize. 
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

/*
 * De-allocates all memory associated with plist.
 */
static void
string_list_destroy(struct string_list *plist)
{
    KASSERT(plist != NULL);
    for (int i = 0; i < plist->used; i++) {
        kfree(plist->list[i]);
    }
    kfree(plist->list);
    kfree(plist);
}

/*
 * Appends a new element at end of list.
 *
 * New memory is not allocated for the element.
 * The list directly references value which must
 * be kmalloc'd by the caller.
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
    char kprogname[PATH_MAX];
    size_t got;
    userptr_t arg;  // User space string.
    char *karg;  // Kernel space string.
    struct string_list *arg_list;
    size_t mem_args;  // Bytes used by args itself and referenced data.
    int result;
    int argc;

    // Copy in arguments from user space to kernel space.
    result = copyinstr(progname, kprogname, PATH_MAX, &got);
    if (result) {
        return result;
    }
    arg_list = string_list_create();
    if (arg_list == NULL) {
        return ENOMEM;
    }
    mem_args = 0;
    argc = 0;
    do {
        if (mem_args > ARG_MAX) {
            string_list_destroy(arg_list);
            return E2BIG;
        }
        // Cannot directly use *args, so use copyin.
        result = copyin(args, (void *)&arg, sizeof(arg));
        if (result) {
            string_list_destroy(arg_list);
            return result;
        }
        if (arg == NULL) {
            break;
        }
        // Temporarily we over-allocate to handle longest args.
        // We will pack more efficienty when we copyout to
        // user space.
        karg = kmalloc(sizeof(char) * PATH_MAX);
        if (karg == NULL) {
            return ENOMEM;
        }
        result = copyinstr(arg, karg, PATH_MAX, &got);
        if (result) {
            string_list_destroy(arg_list);
            return result;
        }
        result = string_list_append(arg_list, karg);
        if (result < 0) {
            string_list_destroy(arg_list);
            return E2BIG;
        }
        argc++;
        // Count string chars and pointer to string against ARG_MAX.
        mem_args += sizeof(char) * got + sizeof(karg);
        // args is userptr_t, so we have to help the math.
        args += sizeof(char *);
    } while (1);
    
    kprintf("execv()\n");
    kprintf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        kprintf("argv[%d] = %s\n", i, arg_list->list[i]);
    }

    string_list_destroy(arg_list);
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