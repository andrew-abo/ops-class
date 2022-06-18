/*
 * file_handle tests.
 */

#include <file_handle.h>
#include <types.h>
#include <lib.h>
#include <clock.h>
#include <test.h>
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

// Tests cannot destory a file_handle if I already hold lock.
int fhtest2(int nargs, char **args) {
    (void)nargs;
    (void)args;

    kprintf_n("Starting fh2...\n");
    fh = create_file_handle("fh");
    KASSERT(fh != NULL);

    lock_acquire(fh->file_lock);
    destroy_file_handle(fh);

	secprintf(SECRET, "Should panic...", "fh2");
    success(TEST161_FAIL, SECRET, "fh2");
    return 1;
}

