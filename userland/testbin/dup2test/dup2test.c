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
	printf("Redirect STDOUT to %s...\n", FILENAME);
	fd = open(FILENAME, O_WRONLY | O_TRUNC | O_CREAT);
	if (fd < 0) {
		err(1, "Cannot open %s for write", FILENAME);
	}
	result = dup2(fd, STDOUT_FILENO);
	if (result != STDOUT_FILENO) {
		err(1, "Expected dup2 result %d, got %d", STDOUT_FILENO, result);
	}
	printf("%s", msg);
	close(fd);

	// Restore STDOUT.
	dup2(STDERR_FILENO, STDOUT_FILENO);

	// Test if reading file matches msg.
	printf("Redirect STDIN from %s...\n", FILENAME);
	fd = open(FILENAME, O_RDONLY);
	if (fd < 0) {
		err(1, "Unable to open %s", FILENAME);
	}
	result = dup2(fd, STDIN_FILENO);
	if (result != STDIN_FILENO) {
		err(1, "Expected dup2 result %d, got %d", STDIN_FILENO, result);
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
	printf("Checking bad calls to dup2...\n");
	result = dup2(-1, 0);
	if (result >= 0) {
		err(1, "dup2(-1, 0) expected -1, got %d", result);
	}
	result = dup2(0, 100000);
	if (result >= 0) {
		err(1, "dup2(0, 100000) expected -1, got %d", result);
	}

	// Test closing a duplicated descriptor does not affect the
	// other.
	printf("Checking closing duplicated descriptor...\n");
	fd = open(FILENAME, O_WRONLY | O_TRUNC | O_CREAT);
	if (fd < 0) {
		err(1, "Cannot open %s for write", FILENAME);
	}
	// Assume 2*fd is unused.
	newfd = 2 * fd;
	result = dup2(fd, newfd);
	if (result != newfd) {
		err(1, "Expected dup2 result %d, got %d", newfd, result);
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
