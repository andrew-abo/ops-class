/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Thread test code.
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <current.h>

#define NTHREADS  8

static struct semaphore *tsem = NULL;

static
void
init_sem(void)
{
	if (tsem==NULL) {
		tsem = sem_create("tsem", 0);
		if (tsem == NULL) {
			panic("threadtest: sem_create failed\n");
		}
	}
}

static
void
loudthread(void *junk, unsigned long num)
{
	int ch = '0' + num;
	int i;

	(void)junk;

	for (i=0; i<120; i++) {
		putch(ch);
	}
	V(tsem);
}

/*
 * The idea with this is that you should see
 *
 *   01234567 <pause> 01234567
 *
 * (possibly with the numbers in different orders)
 *
 * The delay loop is supposed to be long enough that it should be clear
 * if either timeslicing or the scheduler is not working right.
 */
static
void
quietthread(void *junk, unsigned long num)
{
	int ch = '0' + num;
	volatile int i;

	(void)junk;

	putch(ch);
	for (i=0; i<200000; i++);
	putch(ch);

	V(tsem);
}

static
void
runthreads(int doloud)
{
	char name[16];
	int i, result;

	for (i=0; i<NTHREADS; i++) {
		snprintf(name, sizeof(name), "threadtest%d", i);
		result = thread_fork(name, NULL,
				     doloud ? loudthread : quietthread,
				     NULL, i);
		if (result) {
			panic("threadtest: thread_fork failed %s)\n",
			      strerror(result));
		}
	}

	for (i=0; i<NTHREADS; i++) {
		P(tsem);
	}
}


int
threadtest(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	init_sem();
	kprintf("Starting thread test...\n");
	runthreads(1);
	kprintf("\nThread test done.\n");

	return 0;
}

int
threadtest2(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	init_sem();
	kprintf("Starting thread test 2...\n");
	runthreads(0);
	kprintf("\nThread test 2 done.\n");

	return 0;
}

// threadtest3 see tt3.c

// Tests trapframe can be saved and loaded.
int
threadtest4(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	int result;
	char *src_stack;
	char *dst_stack;
	struct trapframe *src_tf;
	struct trapframe *dst_tf;
	struct trapframe *tf_copy;
	struct thread src_thread;
	struct thread dst_thread;

	kprintf("Starting thread test 5...\n");

	// Create mock source thread.
	src_stack = kmalloc(STACK_SIZE);
	KASSERT(src_stack != NULL);
	bzero(src_stack, STACK_SIZE);
	// Arbitrary location on source thread stack.
	src_tf = (struct trapframe *)(src_stack + 512);
	src_thread.t_stack = src_stack;
	thread_checkstack_init(&src_thread);
	thread_checkstack(&src_thread);

	// Create mock destination thread.
	dst_stack = kmalloc(STACK_SIZE);
	KASSERT(dst_stack != NULL);
	bzero(dst_stack, STACK_SIZE);
	dst_thread.t_stack = dst_stack;
	thread_checkstack_init(&dst_thread);
	thread_checkstack(&dst_thread);

	// Copy trapframe from source to destination.
	result = trapframe_save(&tf_copy, src_tf);
	KASSERT(result == 0);
	result = trapframe_load(&dst_thread, &dst_tf, tf_copy);
	KASSERT(result == 0);
	thread_checkstack(&dst_thread);
	kfree(tf_copy);
	kfree(src_stack);
	kfree(dst_stack);
	kprintf("\nThread test 4 done.\n");

	return 0;
}


