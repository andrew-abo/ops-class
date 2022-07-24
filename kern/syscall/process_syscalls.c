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
#include <kern/fcntl.h>
#include <kern/wait.h>
#include <synch.h>
#include <vfs.h>

// Initial number of max strings in list.
#define string_list_INITIAL_MAX 16

// Max number of elements in pointer list after resizing.
#define string_list_MAX_MAX 1024

// Max chars per arg for execv.
#define ARG_SIZE 1024

// An expandable array of strings.
struct string_list {
    int used;  // Number of non-empty elements in list.
    int max;  // Max number of elements in list before resize. 
    size_t size;  // Unaligned total bytes including: string pointers,
                   // chars, \0 terminations.
    char **list;  // Array of pointers.
};

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
    struct proc *parent = curproc;
    struct trapframe *tf_copy;
    int result;

    KASSERT(pid != NULL);
    KASSERT(tf != NULL);
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
    struct proc *proc = curproc;
    KASSERT(pid != NULL);
    spinlock_acquire(&proc->p_lock);
    *pid = proc->pid;
    spinlock_release(&proc->p_lock);
    return 0;
}

/*
 * Exits the current process.
 *
 * This function is shared by different methods of encoding exit status,
 * such as encoding an exception signal or user supplied exitcode.
 * 
 * Args:
 *   exitcode: User supplied exit code.
 */
static void
sys_exit_common(unsigned exit_status)
{
    // curproc becomes NULL once we call proc_remthread, so save it.
    struct proc *proc = curproc;

    spinlock_acquire(&proc->p_lock);
    // We don't support multiple threads per process.  If there is more than
    // one thread active we don't support properly killing them all.
    KASSERT(proc->p_numthreads == 1);
    proc->exit_status = exit_status;
    spinlock_release(&proc->p_lock);
    
    proclist_lock_acquire();
    proclist_reparent(proc->pid);
    proclist_lock_release();

    // Since we confirmed we're the only thread left in the process,
    // it's a bit of overkill to require locks from this point.
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

    proc_zombify(proc);
    thread_exit();
}

/*
 * Exits current process from a signal handler.
 *
 * Args:
 *   code: Signal number, such as SIGSEGV.
 */
void sys_exit_sig(int sig)
{
    sys_exit_common(_MKWAIT_SIG(sig));
}

/*
 * Exits the current process.
 *
 * Args
 *   exitcode: User supplied exit code.
 */
void sys__exit(int exitcode)
{
    sys_exit_common(_MKWAIT_EXIT(exitcode));
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
    struct proc *parent;
    struct proc *child;
    int child_status;
    int result;

    parent = curproc;

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
    if (spinlock_do_i_hold(&parent->p_lock)) {
        // We are attempting to wait for ourself so abort.
        KASSERT(curproc->pid == child->pid);
        spinlock_release(&parent->p_lock);
        return ECHILD;
    }
    spinlock_acquire(&parent->p_lock);
    if (child->ppid != parent->pid) {
        spinlock_release(&parent->p_lock);
        spinlock_release(&child->p_lock);
        return ECHILD;
    }
    spinlock_release(&parent->p_lock);
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
    // inefficient.  A doubly-linked list would eliminate need for two lookups.
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
    plist->size = 0;
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
    size_t string_size;

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
    // Update total bytes including null termination and string pointer.
    // Handle value == NULL which signifies end of argv.
    string_size = value ? strlen(value) + 1 : 0;
    plist->size += sizeof(char) * string_size + sizeof(char *);
    return plist->used++;
}

/*
 * Copy args from user space to kernel space.
 *
 * Args:
 *   args: Array of strings, with last element NULL.
 *   arg_list: Return pointer for allocated list.
 * 
 * Returns:
 *   0 on success, else errno.
 */
static int
copyin_args(userptr_t args, struct string_list **arg_list) 
{
    size_t got;
    userptr_t arg;  // User space string.
    char *karg;  // Kernel space string (oversized)
    char *karg_copy;  // Just sized copy.
    int result;

    KASSERT(arg_list != NULL);
    *arg_list = string_list_create();
    if (*arg_list == NULL) {
        return ENOMEM;
    }
    // Temporarily store incoming args on heap so we don't overflow the stack.
    karg = kmalloc(ARG_SIZE);
    if (karg == NULL) {
        string_list_destroy(*arg_list);
        return ENOMEM;
    }
    do {
        if ((*arg_list)->size > ARG_MAX) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return E2BIG;
        }
        // Cannot directly use *args, so use copyin.
        result = copyin(args, (void *)&arg, sizeof(arg));
        if (result) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return result;
        }
        if (arg == NULL) {
            break;
        }
        result = copyinstr(arg, karg, ARG_SIZE, &got);
        if (result) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return result;
        }
        if (got == ARG_SIZE) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return E2BIG;
        }
        karg_copy = kmalloc(got);
        if (karg_copy == NULL) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return ENOMEM;
        }
        memcpy(karg_copy, karg, got);
        result = string_list_append(*arg_list, karg_copy);
        if (result < 0) {
            kfree(karg);
            string_list_destroy(*arg_list);
            return E2BIG;
        }
        // args is userptr_t, so pointer arithmetic in bytes.
        args += sizeof(char *);
    } while (1);
    kfree(karg);
    // Include NULL pointer argv termination.
    result = string_list_append(*arg_list, (char *)NULL);
    if (result < 0) {
        string_list_destroy(*arg_list);
        return ENOMEM;
    }
    return 0;
}

/*
 * Writes out arg_list to user space stack.
 *
 * Stack be formatted as follows, such that we
 * can pass back argv as a contiguous array of pointers.
 *  
 * arg0\0arg1\0arg2\0\0  <-- highest stack addr
 * NULL
 * arg2_ptr  32b pointer
 * arg1_ptr  32b pointer
 * arg0_ptr  32b pointer <--stackptr
 * 
 * Modifies stackptr to point to arg0_ptr, which can be 
 * used to set argv by the caller, for example.
 * 
 * Args:
 *   arg_list: Pointer to list of strings.
 *   stackptr: Highest address in user stack.
 * 
 * Returns:
 *   New stackptr (set below args copy).
 */ 
static vaddr_t
copyout_args(struct string_list *arg_list, vaddr_t stackptr)
{
    vaddr_t dst;  // Where next byte goes (user space).
    char *src;  // Where next byte comes from (kernel space).
    size_t alignment = sizeof(char *) - 1;
    size_t aligned_size;

    KASSERT(arg_list != NULL);
    KASSERT(stackptr != (vaddr_t)NULL);
    aligned_size = (arg_list->size + alignment) & ~alignment; 
    stackptr -= aligned_size;
    dst = stackptr + sizeof(char *) * arg_list->used;
    
    for (int i = 0; i < arg_list->used; i++) {
        src = arg_list->list[i];
        if (src == NULL) {
            ((char **)stackptr)[i] = (char *)NULL;
            break;
        }
        ((char **)stackptr)[i] = (char *)dst;
        for (src = arg_list->list[i]; *src != '\0'; src++) {
            *(char *)dst++ = *src;
        }
        *(char *)(dst++) = '\0';
    }
    return stackptr;
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
    char *kprogname;
    struct string_list *arg_list;
    size_t got;
    int argc;
    int result;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
    userptr_t argv;

    // Temporarily store incoming args on heap so we don't overflow the stack.
    kprogname = kmalloc(ARG_SIZE);
    if (kprogname == NULL) {
        return ENOMEM;
    }
    result = copyinstr(progname, kprogname, ARG_SIZE, &got);
    if (result) {
        kfree(kprogname);
        return result;
    }
    result = copyin_args(args, &arg_list);
    if (result) {
        kfree(kprogname);
        return result;
    }
    
	/* Open the file. */
	result = vfs_open(kprogname, O_RDONLY, 0, &v);
    kfree(kprogname);
	if (result) {
		return result;
	}
    // TODO(aabo): check if file is executable?

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	vfs_close(v);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	stackptr = copyout_args(arg_list, stackptr);
    argv = (userptr_t)stackptr;
    // Don't count terminating NULL element in arg count.
    argc = arg_list->used - 1;
    KASSERT(argc > 0);
    string_list_destroy(arg_list);
	enter_new_process(argc, argv, NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
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