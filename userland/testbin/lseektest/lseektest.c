/*
 * lseektest.c
 *
 * 	Tests lseek syscall.
 */

#include <unistd.h>
#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <test161/test161.h>

#define FILENAME "lseektest.dat"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char msg[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char buf[128];
	int result;
	int fd;
	off_t pos;

	// Create test file.
	fd = open(FILENAME, O_WRONLY | O_TRUNC | O_CREAT);
	if (fd < 0) {
		err(1, "Cannot open %s for write", FILENAME);
	}
	pos = lseek(fd, 95000, SEEK_SET);
	printf("return pos = %lld\n", pos);
	close(fd);

	// Test if reading file matches msg.
	fd = open(FILENAME, O_RDONLY);
	if (fd < 0) {
		err(1, "Unable to open %s", FILENAME);
	}
	result = read(fd, buf, sizeof(buf));
	if (result != (int)strlen(msg)) {
        err(1, "Expected to read %d bytes, got %d", strlen(msg), result);
	}
	buf[result] = '\0';
	if (strcmp(buf, msg) != 0) {
		err(1, "Expected to read '%s', got '%s'", msg, buf);
	}

	success(TEST161_SUCCESS, SECRET, "/testbin/lseektest");
	return 0;
}
