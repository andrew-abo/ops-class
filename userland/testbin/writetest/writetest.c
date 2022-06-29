/*
 * writetest.c
 *
 * 	Tests write syscall.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <test161/test161.h>

#define FILENAME "writetest.dat"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char msg[] = "hello world\n";
	char buf[128];
	int result;

	result = write(1, msg, sizeof(msg));
	if (result != sizeof(msg)) {
		err(1, "write failed on '%s'.", msg);
	}

	errno = 0;
	result = write(0, msg, sizeof(msg));
	if (result == 0) {
		err(1, "write to STDIN did not return fail status.");
	}
	if (errno != EACCES) {
		err(1, "write to STDIN set errno to %d instead of EACCES.", errno);
	}

	result = write(-1, buf, sizeof(buf));
	if (result == 0) {
		err(1, "write to invalid file descriptor did not return fail status.");
	}
	if (errno != EBADF) {
		err(1, "write to bad file descriptor set errno to %d instead of EBADF.", errno);
	}

	success(TEST161_SUCCESS, SECRET, "/testbin/writetest");	
	return 0;
}
