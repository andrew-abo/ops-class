/*
 * my_forktest
 *
 * Simple fork() tests.
 */

#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{

    pid_t pid;
    (void)argc;
	(void)argv;

	for (int i = 0; i < 1; i++) {
        pid = fork();
        if (pid == 0) {
            // Due to current lack of synchronization, child and
            // parent will race to print their outputs and may be
            // mixed.
            printf("CHILD %d\n", i);
            exit(0);
        }
        printf("Parent spawned child pid = %u", pid);
	}
	nprintf("\n");
	success(TEST161_SUCCESS, SECRET, "/testbin/myy_forktest");
	return 0;
}
