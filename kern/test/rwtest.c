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
static struct semaphore *thread1_ready_sem;
static struct semaphore *join_sem[16];
static int ones;

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
    rwlock_acquire_write(testrwlock);
    shared_value = thread_num;
    for (int i = 0; i < 100; i++) {
        random_yielder(4);
        failif(shared_value != thread_num);
    }
    rwlock_release_write(testrwlock);
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
	secprintf(SECRET, "Should panic...", "rwt2");
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
	secprintf(SECRET, "Should panic...", "rwt3");
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
    spinlock_cleanup(&status_lock);
    return 0;
}

static
void
reader1(void *unused, unsigned long thread_num)
{
    (void)unused;
    rwlock_acquire_read(testrwlock);
    V(thread1_ready_sem);
    failif(shared_value != 0);
    // Waits until writer1 and reader2 are pending.
    P(start_sem);
    random_yielder(4);
    rwlock_release_read(testrwlock);
    V(join_sem[thread_num]);
}

static
void
writer1(void *unused, unsigned long thread_num)
{
    (void)unused;
    rwlock_acquire_write(testrwlock);
    failif(shared_value != 0);
    random_yielder(4);
    // Tries to write a 1 before reader2 reads.
    shared_value = 1;
    rwlock_release_write(testrwlock);
    V(join_sem[thread_num]);
}

static
void
reader2(void *unused, unsigned long thread_num)
{
    (void)unused;
    rwlock_acquire_read(testrwlock);
    random_yielder(4);
    // Counts times writer1 is successful.
    if (shared_value == 1) {
        ones++;
    }
    rwlock_release_read(testrwlock);
    V(join_sem[thread_num]);
}

// Tests readers do not starve writers.
int rwtest6(int nargs, char **args) 
{
	(void)nargs;
	(void)args;
    int result;
    const int TRIES = 10;

    kprintf_n("Starting rwt6...\n");

    test_status = TEST161_SUCCESS;
	spinlock_init(&status_lock);  // supports failif().
    start_sem = sem_create("start_sem", 0);
    thread1_ready_sem = sem_create("thread1_ready", 0);
    KASSERT(start_sem != NULL);
    for (int i = 0; i < 3; i++) {
        join_sem[i] = sem_create("join_sem", 0);
        KASSERT(join_sem[i] != NULL);
    }

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    ones = 0;
    for (int i = 0; i < TRIES; i++) {
        shared_value = 0;
	    result = thread_fork("reader1", NULL, reader1, NULL, 0);
	    if (result) {   
		    panic("rwt6: thread_fork failed: %s\n", strerror(result));
    	}
        P(thread1_ready_sem);
        result = thread_fork("writer1", NULL, writer1, NULL, 1);
        if (result) {   
            panic("rwt6: thread_fork failed: %s\n", strerror(result));
	    }
        // Attempt to starve out writer1.
	    result = thread_fork("reader2", NULL, reader2, NULL, 2);
	    if (result) {   
		    panic("rwt6: thread_fork failed: %s\n", strerror(result));
	    }
        // Starts reader1.
        V(start_sem);
        // Waits for all threads to finish.
        for (int j = 0; j < 3; j++) {
            P(join_sem[j]);
        }
    }
    // Declare failure if writer1 succeeds less than this fraction.
    kprintf_n("Write succeeded %d/%d\n", ones, TRIES);
    if (ones < TRIES / 3) {
        kprintf_n("rwt6: ones = %d too low\n", ones);
        test_status = TEST161_FAIL;
    }

    // Threads can veto via test_status global.
    kprintf_n("rwt6: test_status = %d\n", test_status);
    success(test_status, SECRET, "rwt6");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;
    sem_destroy(start_sem);
    sem_destroy(thread1_ready_sem);
    start_sem = NULL;
    for (int i = 0; i < 3; i++) {
        sem_destroy(join_sem[i]);
        join_sem[i] = NULL;
    }
    return 0;
}

static
void
writer2(void *unused, unsigned long thread_num)
{
    (void)unused;
    rwlock_acquire_write(testrwlock);
    V(thread1_ready_sem);
    // Waits until reader2 and writer3 are pending.
    P(start_sem);
    shared_value = 0;
    rwlock_release_write(testrwlock);
    V(join_sem[thread_num]);
}

static
void
writer3(void *unused, unsigned long thread_num)
{
    (void)unused;
    rwlock_acquire_write(testrwlock);
    failif(shared_value != 0);
    // Tries to starve out reader2 and write a 1 before
    // reader2 can read a 0.
    shared_value = 1;
    rwlock_release_write(testrwlock);
    V(join_sem[thread_num]);
}

// Tests writers do not starve readers.
int rwtest7(int nargs, char **args) {
	(void)nargs;
	(void)args;
    int result;
    const int TRIES = 10;
    int successes;

    kprintf_n("Starting rwt7...\n");

    test_status = TEST161_SUCCESS;
	spinlock_init(&status_lock);  // supports failif().
    start_sem = sem_create("start_sem", 0);
    thread1_ready_sem = sem_create("thread1_ready", 0);
    KASSERT(start_sem != NULL);
    for (int i = 0; i < 3; i++) {
        join_sem[i] = sem_create("join_sem", 0);
        KASSERT(join_sem[i] != NULL);
    }

    testrwlock = rwlock_create("testrwlock");
    KASSERT(testrwlock != NULL);
    ones = 0;
    for (int i = 0; i < TRIES; i++) {
        shared_value = 0;
        result = thread_fork("writer2", NULL, writer2, NULL, 0);
        if (result) {   
            panic("rwt7: thread_fork failed: %s\n", strerror(result));
	    }
        P(thread1_ready_sem);
	    result = thread_fork("reader2", NULL, reader2, NULL, 1);
	    if (result) {   
		    panic("rwt7: thread_fork failed: %s\n", strerror(result));
	    }
        // Attempts to starve out reader2.
        result = thread_fork("writer3", NULL, writer3, NULL, 2);
        if (result) {   
            panic("rwt7: thread_fork failed: %s\n", strerror(result));
	    }
        // Starts writer2.
        V(start_sem);
        // Waits for all threads to finish.
        for (int j = 0; j < 3; j++) {
            P(join_sem[j]);
        }
    }
    // Declare failure if reader2 starves more than this fraction.
    successes = TRIES - ones;
    kprintf_n("Reader succeeded %d/%d\n", successes, TRIES);
    if (successes < TRIES / 3) {
        kprintf("rwt7: successes = %d too low.", successes);
        test_status = TEST161_FAIL;
    }

    // Threads can veto via test_status global.
    kprintf_n("rwt7: test_status = %d", test_status);
    success(test_status, SECRET, "rwt7");
    rwlock_destroy(testrwlock);
    testrwlock = NULL;
    sem_destroy(start_sem);
    sem_destroy(thread1_ready_sem);
    start_sem = NULL;
    for (int i = 0; i < 3; i++) {
        sem_destroy(join_sem[i]);
        join_sem[i] = NULL;
    }
    spinlock_cleanup(&status_lock);
    return 0;
}


