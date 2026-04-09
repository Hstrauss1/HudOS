//Hudson Strauss
#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "spinlock.h"

#define SEM_WAITQ_MAX 16

typedef struct {
	int count;
	spinlock_t lock;
	int waitq[SEM_WAITQ_MAX]; // task IDs waiting
	int wait_head;
	int wait_tail;
} semaphore_t;

// initialize with a given count (e.g. 1 = mutex, 0 = event, N = resource pool)
void sem_init(semaphore_t *s, int count);

// decrement: if count > 0, proceed; otherwise sleep until signaled
void sem_wait(semaphore_t *s);

// increment: wake one waiting task if any, otherwise increment count
void sem_signal(semaphore_t *s);

// try to decrement without sleeping, returns 1 if acquired, 0 if would block
int sem_trywait(semaphore_t *s);

#endif
