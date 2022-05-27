/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

// Number males available.
static struct semaphore *males_avail_sem;

// Number of males allowed to mate now.
static struct semaphore *males_can_mate_sem;

// Number of females available.
static struct semaphore *females_avail_sem;

// Number of females allowed to mate now.
static struct semaphore *females_can_mate_sem;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	if ((males_avail_sem = sem_create("males_avail", 0)) == NULL) {
		panic("Cannot create males_avail_sem.");
	}
	if ((males_can_mate_sem = sem_create("males_can_mate", 0)) == NULL) {
		panic("Canot create males_can_mate_sem.");
	}
	if ((females_avail_sem = sem_create("females_avail", 0)) == NULL) {
		panic("Cannot create females_avail_sem");
	}
	if ((females_can_mate_sem = sem_create("females_can_mate", 0)) == NULL) {
		panic("Cannot create females_can_mate_sem");
	}
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	sem_destroy(males_avail_sem);
	sem_destroy(males_can_mate_sem);
	sem_destroy(females_avail_sem);
	sem_destroy(females_can_mate_sem);
}

void
male(uint32_t index)
{
	male_start(index);
	V(males_avail_sem);
	P(males_can_mate_sem);
	male_end(index);
}

void
female(uint32_t index)
{
	female_start(index);
	V(females_avail_sem);
	P(females_can_mate_sem);
	female_end(index);
}

void
matchmaker(uint32_t index)
{
	matchmaker_start(index);
	P(males_avail_sem);
	P(females_avail_sem);
	V(males_can_mate_sem);
	V(females_can_mate_sem);
	matchmaker_end(index);
}
