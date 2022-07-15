/*
 * myapp.c
 *
 * 	Simple application to test exec.
 */

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <test161/test161.h>

int
main(int argc, char **argv)
{
	char expected[32];

	printf("myapp running...\n");
	printf("argc = %d\n", argc);
	if (argc != 3) {
		err(1, "Expected argc == 3, got %d\n", argc);
	}
	for (int i = 0; i < argc; i++) {
		printf("argv[%d] = %s\n", i, argv[i]);
		snprintf(expected, sizeof(expected), "arg%d", i);
		if (strcmp(argv[i], expected) != 0) {
			err(1, "Expected argv[%d] = %s, got %s", i, expected, argv[i]);
		}
	}
	success(TEST161_SUCCESS, SECRET, "/testbin/exectest");	
	return 0;
}
