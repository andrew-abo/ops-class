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


// Tests can create and destroy file_handle.
int fhtest1(int nargs, char **args) {
    (void)nargs;
    (void)args;

    struct file_handle *fh;

    kprintf_n("Starting fh1...\n");
    fh = create_file_handle("fh");
    KASSERT(fh != NULL);

    destroy_file_handle(fh);
    
	//secprintf(SECRET, "Should panic...", "rwt2");

    success(TEST161_SUCCESS, SECRET, "fh1");
    //success(TEST161_FAIL, SECRET, "rwt2");
    return 0;
}

