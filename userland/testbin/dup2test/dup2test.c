/*
 * dup2test.c
 *
 * 	Tests dup2 syscall.
 */

#include <unistd.h>
#include <err.h>
#include <string.h>
#include <test161/test161.h>

#define FILENAME "dup2test.dat"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char msg[] = "hello world";
	char msg2[] = "xyzzy";
	char buf[128];
	int result;
	int fd;
	int newfd;

	// Test redirecting STDOUT to file works.
	fd = open(FILENAME, O_WRONLY | O_TRUNC | O_CREAT);
	if (fd < 0) {
		err(1, "Cannot open %s for write", FILENAME);
	}
	result = dup2(fd, STDOUT_FILENO);
	if (result) {
		err(1, "Expected dup2 result 0, got %d", result);
	}
	printf("%s", msg);
	close(fd);

	// Restore STDOUT.
	dup2(STDERR_FILENO, STDOUT_FILENO);

	// Test if reading file matches msg.
	fd = open(FILENAME, O_RDONLY);
	if (fd < 0) {
		err(1, "Unable to open %s", FILENAME);
	}
	result = dup2(fd, STDIN_FILENO);
	if (result) {
		err(1, "Expected dup2 result 0, got %d", result);
	}
	result = read(STDIN_FILENO, buf, sizeof(buf));
	if (result != (int)strlen(msg)) {
        err(1, "Expected to read %d bytes, got %d", strlen(msg), result);
	}
	buf[result] = '\0';
	if (strcmp(buf, msg) != 0) {
		err(1, "Expected to read '%s', got '%s'", msg, buf);
	}


	// Test illegal file descriptor gives error.
	result = dup2(-1, 0);
	if (result == 0) {
		err(1, "dup2(-1, 0) expected non-zero error, got 0");
	}
	result = dup2(0, 100000);
	if (result == 0) {
		err(1, "dup2(0, 100000) expected non-zero error, got 0");
	}

	// Test closing a duplicated descriptor does not affect the
	// other.
	fd = open(FILENAME, O_WRONLY | O_TRUNC | O_CREAT);
	if (fd < 0) {
		err(1, "Cannot open %s for write", FILENAME);
	}
	// Assume 2*fd is unused.
	newfd = 2 * fd;
	result = dup2(fd, newfd);
	if (result) {
		err(1, "Expected dup2 result 0, got %d", result);
	}
	close(fd);
	// Should still be able to write to newfd which references same file handle.
	result = write(newfd, msg2, sizeof(msg2));
	if (result != sizeof(msg2)) {
		err(1, "Expected to write %d bytes, got %d", sizeof(msg2), result);
	}
	// Test if reading file matches msg2.
	fd = open(FILENAME, O_RDONLY);
	if (fd < 0) {
		err(1, "Unable to open %s", FILENAME);
	}
	result = read(fd, buf, sizeof(buf));
	if (result != sizeof(msg2)) {
        err(1, "Expected to read %d bytes, got %d", sizeof(msg2), result);
	}
	buf[result] = '\0';
	if (strcmp(buf, msg2) != 0) {
		err(1, "Expected to read '%s', got '%s'", msg2, buf);
	}	

	success(TEST161_SUCCESS, SECRET, "/testbin/dup2test");
	return 0;
}
