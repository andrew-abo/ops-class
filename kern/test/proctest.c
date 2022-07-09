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
	int i;
	int result;

	// Test creating a sequential list of pids.
	for (i = 0; i < NEWPROCS; i++) {
		newproc[i] = proc_create("new");
		KASSERT(newproc[i] != NULL);
		result = proclist_insert(newproc[i]);
		KASSERT(result == 0);
		KASSERT(newproc[i]->pid == PID_MIN + i);
	}

	// Delete one pid in the middle.
	i = 2;
	p = proclist_remove(PID_MIN + i);
	KASSERT(p != NULL);
	proc_destroy(p);

	// Insert into the gap.
	p = proc_create("new");
	KASSERT(p != NULL);
	result = proclist_insert(p);
	KASSERT(result == 0);
	KASSERT(p->pid == PID_MIN + i);
	
	// Delete all the procs.
	for (i = 0; i < NEWPROCS; i++) {
		p = proclist_remove(PID_MIN + i);
		KASSERT(p != NULL);
		proc_destroy(p);
	}

	kprintf_t("\n");
	success(TEST161_SUCCESS, SECRET, "proc2");

	return 0;
}
