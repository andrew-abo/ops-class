/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <kern/errno.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <synch.h>
#include <vnode.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

// Linked list of user processes.
static struct proc *proclist = NULL;
static struct lock *proclist_lock;

static void
proc_abort(struct proc *proc)
{
	if (proc == NULL) {
		return;
	}
	if (proc->p_cwd_lock) {
		lock_destroy(proc->p_cwd_lock);
	}
	if (proc->waitpid_lock) {
		lock_destroy(proc->waitpid_lock);
	}
	if (proc->waitpid_cv) {
		cv_destroy(proc->waitpid_cv);
	}
	if (proc->files_lock) {
		lock_destroy(proc->files_lock);
	}
	if (proc->p_name) {
		kfree(proc->p_name);
	}
	kfree(proc);
}

/*
 * Create a proc structure.
 *
 * Note this does not populate the fields with viable values,
 * it just allocates all the data structures.
 */
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(struct proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->pid = 0;
	proc->ppid = 0;
	proc->p_numthreads = 0;
	proc->p_state = S_READY;
	proc->exit_status = 0;
	proc->p_name = NULL;
	proc->p_cwd = NULL;
	proc->p_cwd_lock = NULL;
	proc->waitpid_lock = NULL;
	proc->waitpid_cv = NULL;
	proc->p_addrspace = NULL;
	proc->files_lock = NULL;
	proc->next = NULL;
	for (int fd = 0; fd < FILES_PER_PROCESS_MAX; fd++) {
		proc->files[fd] = NULL;
	}
	
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		proc_abort(proc);
		return NULL;
	}
	proc->waitpid_cv = cv_create("waitpid");
	if (proc->waitpid_cv == NULL) {
		proc_abort(proc);
		return NULL;
	}
	proc->waitpid_lock = lock_create("waitpid");
	if (proc->waitpid_lock == NULL) {
		proc_abort(proc);
		return NULL;
	}
	proc->p_cwd_lock = lock_create("p_cwd");
	if (proc->p_cwd_lock == NULL) {
		proc_abort(proc);
		return NULL;
	}
	proc->files_lock = lock_create("files");
	if (proc->files_lock == NULL) {
		proc_abort(proc);
		return NULL;
	}
	spinlock_init(&proc->p_lock);

	return proc;
}

/*
 * De-allocate everything except fields needed for waitpid.
 *
 * Prepare to transition process to zombie, but don't change p_state yet.
 *
 * Args:
 *   proc: Pointer to process to prepare to be zombie.
 */
void
proc_pre_zombie(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	spinlock_acquire(&proc->p_lock);

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;
		
		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		} else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		spinlock_release(&proc->p_lock);
		as_destroy(as);
		spinlock_acquire(&proc->p_lock);
	}
	if (proc->p_cwd_lock) {
        lock_destroy(proc->p_cwd_lock);
		proc->p_cwd_lock = NULL;
	}
	if (proc->files_lock) {
		lock_destroy(proc->files_lock);
		proc->files_lock = NULL;
	}

	spinlock_release(&proc->p_lock);
}

/*
 * Destroy a proc structure.
 *
 * De-allocates everything.  Should not be used to make
 * a zombie because you will lose the fields needed for
 * waitpid.
 * 
 * Args:
 *   proc: Pointer to process to destroy.
 */
void
proc_destroy(struct proc *proc)
{
	// In case proc did not exit on its own, we may need to
	// de-allocate proc member data structures.
    proc_pre_zombie(proc);
	if (proc->waitpid_lock) {
        lock_destroy(proc->waitpid_lock);
	}
	if (proc->waitpid_cv) {
        cv_destroy(proc->waitpid_cv);
	}
	if (proc->p_name) {
		kfree(proc->p_name);
	}
	spinlock_cleanup(&proc->p_lock);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Copy file descriptor table of curproc to newproc.
 */
void
copy_file_descriptor_table(struct proc *dst, const struct proc *src)
{
	struct file_handle *fh;

	KASSERT(src != NULL);
	KASSERT(dst != NULL);
	
    for (int fd = 0; fd < FILES_PER_PROCESS_MAX; fd++) {
		fh = src->files[fd];
		dst->files[fd] = fh;
		if (fh != NULL) {
			lock_file_handle(fh);
			fh->ref_count++;
			release_file_handle(fh);
		}
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 * 
 * This process is effectively the "init" process from which
 * all other processes are forked.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *proc = curproc;
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&proc->p_lock);
	if (proc->p_cwd != NULL) {
		VOP_INCREF(proc->p_cwd);
		newproc->p_cwd = proc->p_cwd;
	}
	spinlock_release(&proc->p_lock);

	lock_acquire(proc->files_lock);
	copy_file_descriptor_table(newproc, proc);
	lock_release(proc->files_lock);

	/* Process fields */
	newproc->pid = 1;

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

// Next never used process ID.
static pid_t next_pid = PID_MIN;

static struct lock *next_pid_lock;

// Returns positive value if a new pid is available, else 0.
pid_t 
new_pid()
{
	pid_t pid;

	lock_acquire(next_pid_lock);
	if (next_pid <= PID_MAX) {
		pid = next_pid++;
	} else {
		pid = 0;
	}
	lock_release(next_pid_lock);
    return pid;
}

void init_pid_list()
{
	next_pid_lock = lock_create("pid_list");
	if (next_pid_lock == NULL) {
		panic("Could not initialize pid list.");
	}
}

void teardown_pid_list()
{
	lock_destroy(next_pid_lock);
}

/*
 * Insert newproc into proclist (unsorted).
 *
 * Args:
 *   newproc: Pointer to new process to insert..
 */   
void
proclist_insert(struct proc *newproc)
{
	KASSERT(newproc != NULL);
	proclist_lock_acquire();
	newproc->next = proclist;
	proclist = newproc;
	proclist_lock_release();
}

/*
 * Removes process with specified pid from proclist.
 *
 * Does not modify the process or free any memory.
 * 
 * Args:
 *   pid: Process ID to remove.
 * 
 * Returns:
 *   Pointer to proc removed else NULL if not found.
 */
struct proc *proclist_remove(pid_t pid)
{
	struct proc *p;
	struct proc *prev;

	KASSERT((pid >= 1) && (pid <= PID_MAX));
	proclist_lock_acquire();
	prev = NULL;
	for (p = proclist; p != NULL; p = p->next) {
		if (p->pid == pid) {
            if (prev == NULL) {
				proclist = p->next;
				proclist_lock_release();
				return p;
			}
			prev->next = p->next;
			proclist_lock_release();
			return p;
		}
		prev = p;
	}
	proclist_lock_release();
	return NULL;
}

/*
 * Initalizes global process list.
 *
 * Returns:
 *   0 on success, else 1.
 */
void proclist_init()
{
    proclist = NULL;
	proclist_lock = lock_create("proclist");
	if (proclist_lock == NULL) {
		panic("Cannot create proclist_lock.");
	}
}

void proclist_teardown()
{
	KASSERT(proclist_lock != NULL);
	lock_destroy(proclist_lock);
}

void proclist_lock_acquire()
{
	lock_acquire(proclist_lock);
}

void proclist_lock_release()
{
	lock_release(proclist_lock);
}

/*
 * Re-assigns children of pid to the init process (pid=1).
 *
 * Args:
 *   pid: Pid of exiting parent.
 */
void proclist_reparent(pid_t pid)
{
	struct proc *p, *prev;

	proclist_lock_acquire();
	for (p = proclist; p != NULL;) {
		spinlock_acquire(&p->p_lock);
		if (p->ppid == pid) {
			p->ppid = 1;
		}
		prev = p;
		p = p->next;
		spinlock_release(&prev->p_lock);
	}
	proclist_lock_release();
}

/*
 * Returns proc from proclist matching pid.
 *
 * Args:
 *   pid: Process ID to find.b
 * 
 * Returns:
 *   Pointer to proc matching pid, else NULL.
 */
struct proc 
*proclist_lookup(pid_t pid) 
{
    struct proc *p;

    proclist_lock_acquire();
	for (p = proclist; p != NULL; p = p->next) { 
		if (p->pid == pid) {
			proclist_lock_release();
			return p;
		}
	}
	proclist_lock_release();
	return NULL;
}

/*
 * Prints proclist as debugging aid.
 */
void
proclist_print()
{
	struct proc *p;
	char s_run[] = "RUN";
	char s_ready[] = "READY";
	char s_sleep[] = "SLEEP";
	char s_zombie[] = "ZOMBIE";
	char s_unknown[] = "UNKNOWN";
	char *state;

	proclist_lock_acquire();
	kprintf("%6s %6s %30s %10s\n", "PID", "PPID", "NAME", "STATE");
	for (p = proclist; p != NULL; p = p->next) {
		switch (p->p_state) {
			case S_RUN:
			state = s_run;
			break;
			
			case S_READY:
			state = s_ready;
			break;
			
			case S_SLEEP:
			state = s_sleep;
			break;
			
			case S_ZOMBIE:
			state = s_zombie;
			break;
			
			default:
			state = s_unknown;
			break;
        }
		kprintf("%6d %6d %30s %10s\n", p->pid, p->ppid, p->p_name, state);
	}
	proclist_lock_release();
}