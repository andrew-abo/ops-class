/*
 * exittest.c
 *
 * 	Tests exit syscall.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <test161/test161.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	exit(0);

	success(TEST161_FAIL, SECRET, "/testbin/exittest");	
	return 0;
}
