/*
 * readtest.c
 *
 * 	Tests read syscall.
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <test161/test161.h>

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char buf[128];
	int result;

	result = read(0, buf, sizeof(buf)-1);
	if (result < 0) {
		err(1, "read failed on STDIN.");
	}
	buf[result] = '\0';
	tprintf("read: %s\n", buf);

	errno = 0;
	result = read(1, buf, sizeof(buf));
	if (result == 0) {
		err(1, "read STDOUT did not return fail status.");
	}
	if (errno != EACCES) {
		err(1, "read STDOUT set errno to %d instead of EACCES.", errno);
	}

	result = read(-1, buf, sizeof(buf));
	if (result == 0) {
		err(1, "read to invalid file descriptor did not return fail status.");
	}
	if (errno != EBADF) {
		err(1, "read to bad file descriptor set errno to %d instead of EBADF.", errno);
	}

	return 0;
}
