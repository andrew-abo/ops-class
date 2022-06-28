/*
 * file_handle tests.
 */

#include <file_handle.h>
#include <lib.h>
#include <test.h>
#include <kern/fcntl.h>
#include <kern/secret.h>
#include <kern/test161.h>


static struct file_handle *fh;

// Tests can create and destroy file_handle.
int fhtest1(int nargs, char **args) {
    (void)nargs;
    (void)args;

    kprintf_n("Starting fh1...\n");
    fh = create_file_handle("fh");
    KASSERT(fh != NULL);

    destroy_file_handle(fh);
    fh = NULL;

    success(TEST161_SUCCESS, SECRET, "fh1");
    return 0;
}

// Tests can open and close file handle.
int fhtest2(int nargs, char **args) {
    (void)nargs;
    (void)args;

    int result;

    kprintf_n("Starting fh2...\n");
    result = open_file_handle("con:", O_RDONLY, &fh);
    KASSERT(result == 0);
    KASSERT(fh != NULL);
    KASSERT(fh->flags == O_RDONLY);
    KASSERT(fh->ref_count == 0);
    KASSERT(fh->offset == 0);
    KASSERT(fh->vn != NULL);
    close_file_handle(fh);
    fh = NULL;

    success(TEST161_SUCCESS, SECRET, "fh2");
    return 0;
}

// Tests cannot close a file_handle if I already hold lock.
int fhtest3(int nargs, char **args) {
    (void)nargs;
    (void)args;

    int result;

    kprintf_n("Starting fh3...\n");
    result = open_file_handle("con:", O_WRONLY, &fh);
    KASSERT(result == 0);
    KASSERT(fh != NULL);

    lock_acquire(fh->file_lock);
    close_file_handle(fh);

	secprintf(SECRET, "Should panic...", "fh3");
    success(TEST161_FAIL, SECRET, "fh3");
    return 1;
}

