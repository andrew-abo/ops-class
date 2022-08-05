/*
 * my_forktest
 *
 * Simple fork() tests.
 */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>
#include <stdlib.h>

#define N_CHILD 100

int main(int argc, char *argv[])
{

    (void)argc;
	(void)argv;
    pid_t pid[N_CHILD];
	int result;
	int status;

	for (int i = 0; i < N_CHILD; i++) {
        pid[i] = fork();
		if (pid[i] < 0) {
			err(1, "fork failed\n");
		}
        if (pid[i] == 0) {
            printf("CHILD %d\n", i);
            exit(1);
        }
	}

	for (int i = 0; i < N_CHILD; i++) {
		result = waitpid(pid[i], &status, 0);
		if (result < 0) {
			err(1, "waitpid returned errno = %d", errno);
		}
        printf("Parent spawned child pid = %u\n", pid[i]);
		printf("Child exit status was %d\n", status);
	}
	nprintf("\n");
	success(TEST161_SUCCESS, SECRET, "/testbin/my_forktest");
	return 0;
}
