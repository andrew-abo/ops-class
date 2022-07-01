/*
 * chdirtest.c
 *
 * 	Tests chdir syscall.
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

	int result;

	errno = 0;
	result = chdir(".");
	if (result < 0) {
		err(1, "chdir returned errno %d", errno);
	}

	success(TEST161_SUCCESS, SECRET, "/testbin/getcwdtest");	
	return 0;
}
