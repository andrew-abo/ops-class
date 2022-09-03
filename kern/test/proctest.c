/*
 * Process management test code.
 *
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <proc.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

#define CREATELOOPS 4
#define NEWPROCS 5
#define N_PID 120


// Tests proc objects can be created and destroyed.
int
proctest1(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	int i;
	struct proc *proc;

	kprintf_n("Starting proc1...\n");
	for (i=0; i<CREATELOOPS; i++) {
		kprintf_t(".");
		proc = proc_create("testproc");
		if (proc == NULL) {
			panic("proc1: proc_create failed\n");
		}
        proc_destroy(proc);
	}

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "proc1");

	return 0;
}

// Tests processes can be added and removed from proclist.
int
proctest2(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	struct proc *newproc[NEWPROCS];
	struct proc *p;
	pid_t pids[NEWPROCS];
	int i;
	int result;

	// Test creating a sequential list of pids.
	for (i = 0; i < NEWPROCS; i++) {
		newproc[i] = proc_create("new");
		newproc[i]->pid = new_pid();
		pids[i] = newproc[i]->pid;
		KASSERT(newproc[i] != NULL);
		result = proclist_insert(newproc[i]);
		KASSERT(result == 0);
	}

	// Delete one pid in the middle.
	p = proclist_remove(pids[2]);
	KASSERT(p != NULL);
	proc_destroy(p);

	p = proc_create("new");
	KASSERT(p != NULL);
	result = proclist_insert(p);
	KASSERT(result == 0);
	
	// Delete all the procs.
	for (i = 0; i < NEWPROCS; i++) {
		p = proclist_remove(pids[i]);
		if (i != 2) {
            KASSERT(p != NULL);
            proc_destroy(p);
		}
	}

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "proc2");

	return 0;
}

// Tests new PIDs can be generated.
int
proctest3(int nargs, char **args)
{
	(void)nargs;
	(void)args;
	int i;
	pid_t pid, prev;

	prev = 0;
	for (i = 0; i < 10; i++) {
        pid = new_pid();
		KASSERT(pid > prev);
		prev = pid;
	}

	// Exhaust pids.
	for (i = 0; i <= PID_MAX; i++) {
		pid = new_pid();
	}
	// No more PIDs left.
	KASSERT(pid == 0);

	success(TEST161_SUCCESS, SECRET, "proc3");
	return 0;
}