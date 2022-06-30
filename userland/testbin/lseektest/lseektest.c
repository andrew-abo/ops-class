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
	result = write(fd, msg, strlen(msg));
	if (result < (int)strlen(msg)) {
		err(1, "Error writing to %s", FILENAME);
	}
	close(fd);

	// Test SEEK_SET sets position correctly.
	fd = open(FILENAME, O_RDONLY);
	if (fd < 0) {
		err(1, "Cannot open %s for read", FILENAME);
	}
	pos = lseek(fd, 5, SEEK_SET);
	if (pos != 5) {
		err(1, "Expected seek to 5, got %llu", pos);
	}
	result = read(fd, buf, 1);
	if (buf[0] != '5') {
		err(1, "Expected to read '5', got %c", buf[0]);
	}
	pos = lseek(fd, -2, SEEK_CUR);
	if (pos != 4) {
		err(1, "Expected seek to 4, got %llu", pos);
	}
	result = read(fd, buf, 1);
	if (buf[0] != '4') {
		err(1, "Expected to read '4', to %c", buf[0]);
	}
	pos = lseek(fd, -1, SEEK_END);
	if (pos != (int)strlen(msg) - 1) {
		err(1, "Expected seek to %d, got %llu", strlen(msg) - 1, pos);
	}
	result = read(fd, buf, 1);
	if (buf[0] != 'Z') {
		err(1, "Expected to read 'Z', got %c", buf[0]);
	}
	close(fd);

	success(TEST161_SUCCESS, SECRET, "/testbin/lseektest");
	return 0;
}
