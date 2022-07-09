/*
 * getpidtest.c
 *
 * 	Tests getpid syscall.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <test161/test161.h>

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	getpid();

	success(TEST161_SUCCESS, SECRET, "/testbin/getpidtest");	
	return 0;
}
