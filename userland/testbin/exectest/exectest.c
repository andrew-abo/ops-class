/*
 * execttest.c
 *
 * 	Tests execv system call.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <test161/test161.h>

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char *args[4];
	char arg0[] = "arg0";
	char arg1[] = "arg1";
	char arg2[] = "arg2";
	char **my_args;
	
	args[0] = arg0;
	args[1] = arg1;
	args[2] = arg2;
	args[3] = NULL;
	my_args = args;

	//for (char **p = my_args; *p != NULL; p++) {
	//	printf("%s\n", *p);
	//}
	execv("test", my_args);


	success(TEST161_SUCCESS, SECRET, "/testbin/exectest");	
	return 0;
}
