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

// Max allowed number of execv arguments.
// This is arbitrary and should not limit use of ARG_MAX bytes total
// size of arguments set by system.
#define ARGC_MAX 4096

// Max chars for execv progname.
// This is arbitrary and should not limit use of ARG_MAX bytes total
// size of arguments set by system.
#define PROGNAME_MAX 1024

// Initial size in bytes of args image for execv.
// We dynamically adjust this upwards to ARG_MAX as needed.
#define ARG_INITIAL_SIZE 8192

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
    struct file_handle *fh;

    KASSERT(pid != NULL);
    KASSERT(tf != NULL);
    child = proc_create("fork");
    if (child == NULL) {
        return ENOMEM;
    }
    result = as_copy(parent->p_addrspace, &(child->p_addrspace));
    if (result) {
        proc_destroy(child);
        return result;
    }

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

    lock_acquire(parent->files_lock);
    copy_file_descriptor_table(child, parent);
    lock_release(parent->files_lock);

    child->pid = new_pid();
    proclist_insert(child);

    // Child returns via enter_forked_process().
    result = thread_fork("fork", child, enter_forked_process, (void *)tf_copy, 0);
    if (result) {
        kfree(tf_copy);
        proclist_remove(child->pid);
        for (int fd = 0; fd < FILES_PER_PROCESS_MAX; fd++) {
            fh = child->files[fd];
            child->files[fd] = NULL;
            if (fh != NULL) {
                // Can't use sys_close() here because child != curproc. 
                // Unfortunately, we have to duplicate some code for closing open descriptors.
                lock_file_handle(fh);
                fh->ref_count--;
                if (fh->ref_count == 0) {
                    vfs_close(fh->vn);
                    release_file_handle(fh);
                    destroy_file_handle(fh);
                    continue;
                }
                release_file_handle(fh);
            }
        }
        proc_destroy(child);
        return result;
    }
    // Parent returns child pid.
    *pid = child->pid;
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
    
    proclist_reparent(proc->pid);

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

    proc_remthread(curthread);
    proc_pre_zombie(proc);

    spinlock_acquire(&proc->p_lock);
	if (proc->p_numthreads == 0) {
        lock_acquire(proc->waitpid_lock);
        cv_broadcast(proc->waitpid_cv, proc->waitpid_lock);
        lock_release(proc->waitpid_lock);
        // Change p_state to zombie only after all accesses to proc
        // are complete, which signals parent it is ok to destroy.
        proc->p_state = S_ZOMBIE;
	}
	spinlock_release(&proc->p_lock);

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
    child = proclist_lookup(pid);

    if (child == NULL) {
        return ESRCH;
    }

    if (parent == child) {
        // We are attempting to wait for ourself so abort.
        return ECHILD;
    }

    spinlock_acquire(&child->p_lock);

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
    KASSERT(child->p_state == S_ZOMBIE);
    child_status = child->exit_status;

    spinlock_release(&child->p_lock);

    // TODO(aabo): Looking through the linked list above and here is
    // inefficient.  A doubly-linked list would eliminate need for two lookups.
    proclist_remove(pid);

    // Child exit call must be guaranteed to be done with all accesses
    // to its proc struct before we destroy it.
    proc_destroy(child);
    if (status != NULL) {
        result = copyout(&child_status, status, sizeof(int));
        if (result) {
            return result;
        }
    }
    return 0;
}

struct args_image {
    int argc;  // Total number of args including progname.
    size_t used;  // Total data used bytes: string pointers, chars, null terminations.
    size_t size;  // Total data allocated bytes.
    char **data;  // String pointers or chars.
};

/*
 * Copy args from user space to kernel space.
 *
 * arg0\0arg1\0arg2\0\0
 * NULL
 * arg2_ptr  32b pointer
 * arg1_ptr  32b pointer
 * arg0_ptr  32b pointer <-- image->data
 * 
 * Args:
 *   args: Array of strings, with last element NULL.
 *   args_image: Pre-allocated pointer to empty image of string pointers
 *     and char data.
 *   argc: Return pointer to number of non-NULL arguments.
 *   args_size: Return pointer to number of total bytes used.
 * 
 * Returns:
 *   0 on success, else errno.
 */
static int
copyin_args(userptr_t args, struct args_image *image)
{
    char **arg;  // Pointer to next argument to copy in.
    char *dst;  // Kernel space destination for chars.
    int nargs = 0;  // Number of arguments.
    size_t got;
    int result;
    int bytes_avail;  // Bytes available before exceeding ARG_MAX.

    // First pass: count number of args and fetch arg pointers into kernel space.
    image->used = 0;  // Bytes consumed.
    arg = (char **)args;
    for (nargs = 0; nargs < ARGC_MAX; nargs++) {
        result = copyin((userptr_t)arg, (void *)&image->data[nargs], sizeof(char *));
        if (result) {
            return result;
        }
        image->used += sizeof(char *);
        if (image->data[nargs] == (char *)NULL) {
            break;
        }
        arg++;
    }
    if (nargs == ARGC_MAX) {
        return E2BIG;
    }
    image->argc = nargs;
    dst = (char *)(image->data) + image->used;
    // Second pass: fetch chars into kernel space.
    for (int n = 0; n < nargs; n++) {
        bytes_avail = image->size - image->used;
        if (bytes_avail <= 0) {
            return E2BIG;
        }
        result = copyinstr((userptr_t)image->data[n], dst, bytes_avail, &got);
        if (result) {
            return result;
        }
        image->used += got;
        image->data[n] = dst;
        dst += got;
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
 *   image: Pointer to image to copy. 
 *   stackptr: Highest address in user stack.
 * 
 * Returns:
 *   New stackptr (set below args copy).
 */ 
static vaddr_t
copyout_args(struct args_image image, vaddr_t stackptr)
{
    vaddr_t dst;  // Where next char goes (user space).
    char *src;  // Where next char comes from (kernel space).
    size_t alignment = sizeof(char *) - 1;
    size_t aligned_size;

    KASSERT(stackptr != (vaddr_t)NULL);
    aligned_size = (image.size + alignment) & ~alignment; 
    stackptr -= aligned_size;
    // First char is stored just above the NULL argument.
    dst = stackptr + sizeof(char *) * (image.argc + 1);
    
    for (int n = 0; n < image.argc + 1; n++) {
        src = image.data[n];
        if (src == NULL) {
            ((char **)stackptr)[n] = (char *)NULL;
            break;
        }
        ((char **)stackptr)[n] = (char *)dst;
        for (src = image.data[n]; *src != '\0'; src++) {
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
    struct args_image image;
    size_t got;
    int result;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
    userptr_t argv;
    struct addrspace *old_as;
    size_t data_size;

    kprogname = kmalloc(PROGNAME_MAX);
    if (kprogname == NULL) {
        return ENOMEM;
    }
    result = copyinstr(progname, kprogname, PROGNAME_MAX, &got);
    if (result) {
        kfree(kprogname);
        return result;
    }
    if (got >= PROGNAME_MAX) {
        kfree(kprogname);
        return E2BIG;
    }

    // To avoid a memory shortage when forking many processes at the same time,
    // start data_size small and grow as needed.
    data_size = ARG_INITIAL_SIZE;
    do {
        image.data = kmalloc(data_size);
        if (image.data == NULL) {
            kfree(kprogname);
            return ENOMEM;
        }
        image.size = data_size;
        result = copyin_args(args, &image);
        if (result == 0) {
            break;
        }
        kfree(image.data);
        if ((result != E2BIG) && (result != ENAMETOOLONG)) {
            kfree(kprogname);
            return result;
        }
        data_size <<= 1;
    } while (data_size <= ARG_MAX);
    if (data_size > ARG_MAX) {
        kfree(kprogname);
        return E2BIG;
    }
    if (result) {
        kfree(kprogname);
        return result;
    }
    KASSERT(image.argc > 0);

	/* Open the file. */
	result = vfs_open(kprogname, O_RDONLY, 0, &v);
    kfree(kprogname);
	if (result) {
        kfree(image.data);
		return result;
	}
    // TODO(aabo): check if file is executable?

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		kfree(image.data);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	old_as = proc_getas();
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	vfs_close(v);
	if (result) {
        kfree(image.data);
        proc_setas(old_as);
        as_activate();
        as_destroy(as);
		return result;
	}

	/* Define the user heap in the address space */
	result = as_define_heap(as);
	if (result) {
        kfree(image.data);
        proc_setas(old_as);
        as_activate();
        as_destroy(as);     
		return result;
	}

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
        kfree(image.data);
        proc_setas(old_as);
        as_activate();
        as_destroy(as);     
		return result;
	}
    // Point of no return. Discard previous address space.
    as_destroy(old_as);

	stackptr = copyout_args(image, stackptr);
    argv = (userptr_t)stackptr;
    kfree(image.data);
	enter_new_process(image.argc, argv, NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
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
 * Debugging aid.
 *
 * This is not standard.  We hijack the getlogin syscall 
 * so we can run things in the kernel.  It is a custom debugging aid.
 */
void
sys___getlogin()
{
}