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

static bool test_status = TEST161_FAIL;

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
	test_status = TEST161_SUCCESS;

	kprintf_t("\n");
	success(test_status, SECRET, "proc1");

	return 0;
}
