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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <file_handle.h>
#include <limits.h>
#include <spinlock.h>
#include <syscall.h>
#include <thread.h>

struct addrspace;
struct thread;
struct vnode;

typedef threadstate_t procstate_t;

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	pid_t pid;  // My process ID.
	pid_t ppid;  // My parent's process ID.
	unsigned p_numthreads;		/* Number of threads in this process */
	procstate_t p_state;
	int exit_status;  // Only valid if p_state == S_ZOMBIE.
	struct cv *waitpid_cv;  // Wait channel for blocking until I exit.
	struct lock *waitpid_lock;  // Lock for the wait channel.
	struct spinlock p_lock;		/* Lock for this structure */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */
	struct lock *p_cwd_lock;

	// File descriptor table
	struct file_handle *files[FILES_PER_PROCESS_MAX];
	struct lock *files_lock;

	// Process are stored in a linked list sorted by increasing pid.
	struct proc *next;  // Higher pid.
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

void copy_file_descriptor_table(struct proc *dst, const struct proc *src);

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create an empty process object */
struct proc *proc_create(const char *name);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* De-allocate everything except fields needed for waitpid */
void proc_zombify(struct proc *proc);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/* Insert newproc into linked list sorted by pid */ 
int proclist_insert(struct proc *newproc);

/* Remove pid from proclist */
struct proc *proclist_remove(pid_t pid);

/* Re-assigns children of pid to init */
void proclist_reparent(pid_t pid);

void proclist_init(void);
void proclist_teardown(void);
void proclist_lock_acquire(void);
void proclist_lock_release(void);

#endif /* _PROC_H_ */
