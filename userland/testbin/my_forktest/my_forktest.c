/*
 * my_forktest
 *
 * Simple fork() tests.
 */

#include <unistd.h>
//#include <fcntl.h>
//#include <string.h>
//#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>

int main(int argc, char *argv[])
{

    pid_t pid;
    (void)argc;
	(void)argv;

	pid = fork();
	if (pid == 0) {
		// Due to current lack of synchronization, child and
		// parent will race to print their outputs and may be
		// mixed.
		printf("CHILD\n");
		// _exit() not implemented so spin to avoid panic.
		while (1) {
			;
		}
		return 0;
	}
	printf("Parent spawned child pid = %u", pid);
	// _exit() not implemented so spin to avoid panic.
	while (1) {
		;
	}

	nprintf("\n");
	success(TEST161_SUCCESS, SECRET, "/testbin/myy_forktest");
	return 0;
}
