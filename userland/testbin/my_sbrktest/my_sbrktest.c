/*
 * my_sbrktest
 *
 * Simple sbrk() tests.
 */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <test161/test161.h>
#include <stdlib.h>
#include <string.h>

#define MESSAGE "hello world"

int main(int argc, char *argv[])
{

    (void)argc;
	(void)argv;
	char *buf0, *buf;

	buf0 = sbrk(0);
	buf = sbrk(4096);
	snprintf(buf, 4096, MESSAGE);
	if  (strcmp(buf, MESSAGE) != 0) {
		err(1, "String write/read did not match.");
	}
	buf = sbrk(10 * 4096);

	for (int i = 0; i < 10; i++) {
        *(buf + i * 4096) = (char)i;
	}
	for (int i = 0; i < 10; i++) {
        if (*(buf + i * 4096) != (char)i) {
			err(1, "Data corrupted.");
		}
	}
	

	buf = sbrk(0);
	if ((unsigned)(buf - buf0) != 11 * 4096) {
		err(1, "brk did not move by requested amount 10 * 4096 bytes.");
	}
	

	nprintf("\n");
	success(TEST161_SUCCESS, SECRET, "/testbin/my_forktest");
	return 0;
}
