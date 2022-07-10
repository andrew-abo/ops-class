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

int main(int argc, char *argv[])
{

    (void)argc;
	(void)argv;
    pid_t pid[4];
	int result;
	int status;

	for (int i = 0; i < 4; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            printf("CHILD %d\n", i);
            exit(1);
        }
		result = waitpid(pid[i], &status, 0);
		if (result < 0) {
			err(1, "waitpid returned errno = %d", errno);
		}
        printf("Parent spawned child pid = %u\n", pid[i]);
		printf("Child exit status was %d\n", status);
	}

	nprintf("\n");
	success(TEST161_SUCCESS, SECRET, "/testbin/myy_forktest");
	return 0;
}
