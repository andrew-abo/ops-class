/*
 * getcwdtest.c
 *
 * 	Tests __getcwd syscall.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <test161/test161.h>

#define DEFAULTCWD "emu0:"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char buf[128];
	int result;

	errno = 0;
	result = __getcwd(buf, sizeof(buf));
	if (result < 0) {
		err(1, "getcwd returned errno %d", errno);
	}
	buf[result] = '\0';
	if (strcmp(buf, DEFAULTCWD) != 0) {
		err(1, "From getcwd expected %s, got %s", DEFAULTCWD, buf);
	}

	success(TEST161_SUCCESS, SECRET, "/testbin/getcwdtest");	
	return 0;
}
