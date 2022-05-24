/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <kern/test161.h>
#include <spinlock.h>

#define CREATELOOPS 2
#define NSEMLOOPS   2
#define NLOCKLOOPS  2
#define NRWLOOPS    2
#define NTHREADS    24

static struct rwlock *testrwlock = NULL;
static bool test_status = TEST161_FAIL;
struct spinlock status_lock;
static struct semaphore *start_sem;
static struct semaphore *stop_sem;
static struct semaphore *join_sem[16];

// Shared memory.
static volatile unsigned long shared_value = 0;


static
bool
failif(bool condition) {
	if (condition) {
		spinlock_acquire(&status_lock);
		test_status = TEST161_FAIL;
		spinlock_release(&status_lock);
	}
	return condition;
}

static
void
slow_reader(void *unused, unsigned long expected_value)
{
    (void)unused;
    rwlock_acquire_read(testrwlock);
    P(start_sem);
    random_yielder(4);
    failif(shared_value != expected_value);
    rwlock_release_read(testrwlock);
}

static
void
fast_writer(void *unused, unsigned long write_value)
{
    (void)unused;
    rwlock_acquire_write(testrwlock);
    shared_value = write_value;
    rwlock_release_write(testrwlock);
    V(stop_sem);
}

static
void
writer_reader(void *unused, unsigned long thread_num)
{
    (void)unused;
    //rwlock_acquire_write(testrwlock);
    shared_value = thread_num;
    for (int i = 0; i < 100; i++) {
        random_yielder(4);
        failif(shared_value != thread_num);
    }
    //rwlock_release_write(testrwlock);
    V(join_sem[thread_num]);
}

// Tests can create and destroy rwlock.
int rwtest(int nargs, char **args) {
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt1...\n");

    for (int i = 0; i < CREATELOOPS; i++) {
        testrwlock = rwlock_create("testrwlock");
        KASSERT(testrwlock != NULL);
        rwlock_destroy(testrwlock);
    }

    success(TEST161_SUCCESS, SECRET, "rwt1");
    testrwlock = NULL;
    return 0;
}

// Tests if panics correctly when releasing unheld read lock.
int rwtest2(int nargs, char **args) {

	(void)nargs;
	(void)args;

    kprintf_n("Starting rwt2...\n");
    kprintf_n("This test panics on success!\n");

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    rwlock_release_read(testrwlock);
    
    success(TEST161_FAIL, SECRET, "rwt2");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;

    return 0;
}

// Tests if panics correctly when releasing unheld write lock.
int rwtest3(int nargs, char **args) {

	(void)nargs;
	(void)args;

    kprintf_n("Starting rwt3...\n");
    kprintf_n("This test panics on success!\n");

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    rwlock_release_write(testrwlock);
    
    success(TEST161_FAIL, SECRET, "rwt3");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;

    return 0;
}

// Tests reads that start before a write always complete before
// write starts.
int rwtest4(int nargs, char **args) {

	(void)nargs;
	(void)args;
    int result;
    const int READERS = 16;

    kprintf_n("Starting rwt4...\n");

    // Shared memory.
    shared_value = 0;

    test_status = TEST161_SUCCESS;
	spinlock_init(&status_lock);  // supports failif().
    start_sem = sem_create("start_sem", 0);
    stop_sem = sem_create("stop_sem", 0);
    KASSERT(start_sem != NULL);
    KASSERT(stop_sem != NULL);

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    for (int i = 0; i < READERS; i++) {
        // Expected value to read.
		result = thread_fork("reader", NULL, slow_reader, NULL, 0);
		if (result) {   
			panic("rwt4: thread_fork failed: %s\n", strerror(result));
		}
    }
    // Expected value to not read.
    result = thread_fork("writer", NULL, fast_writer, NULL, 0xff);
    if (result) {   
       panic("rwt4: thread_fork failed: %s\n", strerror(result));
	}
    // Start readers, semi-concurrently after writers are created.
    for (int i = 0; i < READERS; i++) {
        V(start_sem);
    }
    // Waits for writer to finish.
    P(stop_sem);
    failif(shared_value != 0xff);

    // Threads can veto via test_status global.
    success(test_status, SECRET, "rwt4");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;
    sem_destroy(start_sem);
    start_sem = NULL;
    sem_destroy(stop_sem);
    stop_sem = NULL;
    return 0;
}

// Tests write collisions do not occur.
int rwtest5(int nargs, char **args) {

	(void)nargs;
	(void)args;
    int result;
    const int WRITERS = 16;

    kprintf_n("Starting rwt5...\n");

    // Shared memory.
    shared_value = 0;

    test_status = TEST161_SUCCESS;
	spinlock_init(&status_lock);  // supports failif().
    for (int i = 0; i < WRITERS; i++) {
        join_sem[i] = sem_create("join_sem", 0);
        KASSERT(join_sem[i] != NULL);
    }

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    for (int i = 0; i < WRITERS; i++) {
        // Expected value to write/read.
		result = thread_fork("writer_reader", NULL, writer_reader, NULL, i);
		if (result) {   
			panic("rwt5: thread_fork failed: %s\n", strerror(result));
		}
    }
    // Waits for all writers to finish.
    for (int i = 0; i < WRITERS; i++) {
        P(join_sem[i]);
    }  

    // Threads can veto via test_status global.
    success(test_status, SECRET, "rwt5");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;
    for (int i = 0; i < WRITERS; i++) {
        sem_destroy(join_sem[i]);
        join_sem[i] = NULL;
    }
    return 0;
}

int rwtest6(int nargs, char **args) {
    (void)nargs;
    (void)args;
    return 0;
}

// TODO(aabo): write some tests to make sure locks
// actually prevent contention to some shared resource
// such as a buffer.

// TODO(aabo): check for starvation of readers or writers.

// Tests multiple readers can hold same lock.
int rwtest7(int nargs, char **args) {
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt7...\n");

    test_status = TEST161_SUCCESS;

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);

    // TODO(aabo): Use multi-thread.
    for (int i = 0; i < 10; i++) {
        rwlock_acquire_read(testrwlock);
    }
    for (int i = 0; i < 10; i++) {
        rwlock_release_read(testrwlock);
    }

    success(test_status, SECRET, "rwt7");

    rwlock_destroy(testrwlock);
    testrwlock = NULL;

    return 0;
}


