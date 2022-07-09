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
		printf("Child\n");
		return 0;
	}
	printf("ABC\n");
	//printf("Parent spawned child pid = %u", pid);

	//nprintf("\n");
	//success(TEST161_SUCCESS, SECRET, "/testbin/myy_forktest");
	return 0;
}
