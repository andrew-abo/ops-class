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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Called by the driver during initialization.
 */

// Mutex to occupy quadrant [i].
static struct lock *quadrant_lock[4];

// Must acquire flow_sem to access flow_state.
static struct cv *flow_cv;

typedef enum { 
	NORTH_SOUTH,  // north-south and right turn concurrency allowed.
	EAST_WEST,    // east-west and right turn concurrency allowed.
	LEFT_TURN,    // No concurrency.
	IDLE          // No cars in intersection.
} flow_t;
static volatile flow_t flow;
static struct lock *flow_lock;

// Number of cars in the intersection.
static volatile int occupancy;
static struct lock *occupancy_lock;



void
stoplight_init() {
	for (int i = 0; i < 4; i++) {
        if ((quadrant_lock[i] = lock_create("quadrant")) == NULL) {
            panic("Cannot create quadrant_lock[%d].", i);
        }
	}
	if ((flow_cv = cv_create("flow")) == NULL) {
		panic("Cannot create flow_cv.");
	}
	if ((flow_lock = lock_create("flow")) == NULL) {
		panic("Cannot create flow_lock.");
	}
	if ((occupancy_lock = lock_create("occupancy")) == NULL) {
		panic("Cannot create occupancy_lock.");
	}
	flow = IDLE;
	occupancy = 0;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	for (int i = 0; i < 4; i++) {
		lock_destroy(quadrant_lock[i]);
	}
	cv_destroy(flow_cv);	
	lock_destroy(flow_lock);
	lock_destroy(occupancy_lock);
}

// Moves car index between quadrants.
// from < 0 does not release quadrant_lock.
static
void
_move(int from, int to, uint32_t index) 
{
	lock_acquire(quadrant_lock[to]);
	inQuadrant(to, index);
	if (from >= 0) {
        lock_release(quadrant_lock[from]);
	}
}

static
void
_enter(int direction, uint32_t index) 
{
	_move(-1, direction, index);
	lock_acquire(occupancy_lock);
	occupancy++;
	lock_release(occupancy_lock);
}

// Thread index leaves intersection from quandrant.
static 
void
_leave(int quadrant, uint32_t index)
{
	leaveIntersection(index);
	lock_release(quadrant_lock[quadrant]);
	lock_acquire(occupancy_lock);
	occupancy--;
	if (occupancy == 0) {
		lock_acquire(flow_lock);
		flow = IDLE;
		cv_broadcast(flow_cv, flow_lock);
		lock_release(flow_lock);
	}
	lock_release(occupancy_lock);
}

void
turnright(uint32_t direction, uint32_t index)
{
	flow_t my_flow;
	
	switch (direction) {
		case 0:
		case 2:
		  my_flow = NORTH_SOUTH;
		  break;
		case 1:
		case 3:
		  my_flow = EAST_WEST;
		  break;
		default:
		  panic("goright(%d, %d): Unknown direction.", direction, index);
	}
	lock_acquire(flow_lock);
	while ((flow != IDLE) && (flow != my_flow)) {
		cv_wait(flow_cv, flow_lock);
	}
	if (flow == IDLE) {
		flow = my_flow;
	}
	cv_broadcast(flow_cv, flow_lock);
	lock_release(flow_lock);

	_enter(direction, index);
	_leave(direction, index);
}

void
gostraight(uint32_t direction, uint32_t index)
{
	flow_t my_flow;
	int prev_quadrant, next_quadrant;

	switch (direction) {
		case 0:
		case 2:
		  my_flow = NORTH_SOUTH;
		  break;
		case 1:
		case 3:
		  my_flow = EAST_WEST;
		  break;
		default:
		  panic("gostraight(%d, %d): Unknown direction.", direction, index);
	}
	lock_acquire(flow_lock);
	while ((flow != IDLE) && (flow != my_flow)) {
		cv_wait(flow_cv, flow_lock);
	}
	if (flow == IDLE) {
		flow = my_flow;
	}
	cv_broadcast(flow_cv, flow_lock);
	lock_release(flow_lock);

	_enter(direction, index);

	// Forward 1.
	prev_quadrant = direction;
	next_quadrant = (prev_quadrant + 3) % 4;
	_move(prev_quadrant, next_quadrant, index);

	_leave(next_quadrant, index);
}

void
turnleft(uint32_t direction, uint32_t index)
{
	int prev_quadrant, next_quadrant;

	// Only allow one car in the intersection during a left turn.
	// We could be a little more efficient and have 4 kinds
	// of left turns, 1 for each direction.
	lock_acquire(flow_lock);
	while (flow != IDLE) {
		cv_wait(flow_cv, flow_lock);
	}
	flow = LEFT_TURN;
	// Waking other threads is useless unless we define all
	// 4 left turns in the future.
	cv_broadcast(flow_cv, flow_lock);
	lock_release(flow_lock);

	_enter(direction, index);

	// Forward 1.
	prev_quadrant = direction;
	next_quadrant = (prev_quadrant + 3) % 4;
	_move(prev_quadrant, next_quadrant, index);

	// Left 1.
	prev_quadrant = next_quadrant;
	next_quadrant = (prev_quadrant + 3) % 4;
	_move(prev_quadrant, next_quadrant, index);

	_leave(next_quadrant, index);
}
