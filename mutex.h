//Hudson Strauss
#ifndef MUTEX_H
#define MUTEX_H

#include "semaphore.h"

// mutex is a binary semaphore (count = 1)
typedef struct {
	semaphore_t sem;
} mutex_t;

static inline void mutex_init(mutex_t *m){
	sem_init(&m->sem, 1);
}

// blocks if held by another task
static inline void mutex_lock(mutex_t *m){
	sem_wait(&m->sem);
}

static inline void mutex_unlock(mutex_t *m){
	sem_signal(&m->sem);
}

// returns 1 if acquired, 0 if already held
static inline int mutex_trylock(mutex_t *m){
	return sem_trywait(&m->sem);
}

#endif
