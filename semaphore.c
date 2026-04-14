//Hudson Strauss
#include "semaphore.h"
#include "task.h"
#include "sched.h"

extern task_t tasks[];
extern int current_task;

void sem_init(semaphore_t *s, int count){
	s->count = count;
	spin_init(&s->lock);
	s->wait_head = 0;
	s->wait_tail = 0;
}

// add task ID to the circular wait queue
static int waitq_push(semaphore_t *s, int id){
	int next = (s->wait_head + 1) % SEM_WAITQ_MAX;
	if(next == s->wait_tail)
		return -1; // full
	s->waitq[s->wait_head] = id;
	s->wait_head = next;
	return 0;
}

// pop next task ID from wait queue, returns -1 if empty
static int waitq_pop(semaphore_t *s){
	if(s->wait_tail == s->wait_head)
		return -1;
	int id = s->waitq[s->wait_tail];
	s->wait_tail = (s->wait_tail + 1) % SEM_WAITQ_MAX;
	return id;
}

void sem_wait(semaphore_t *s){
	while(1){
		unsigned long flags = spin_lock_irqsave(&s->lock);

		if(s->count > 0){
			s->count--;
			spin_unlock_irqrestore(&s->lock, flags);
			return;
		}

		int me = current_task;
		if(waitq_push(s, me) == 0){
			tasks[me].state = TASK_BLOCKED;
			tasks[me].wake_time = 0;
		}

		spin_unlock_irqrestore(&s->lock, flags);
		schedule();
	}
}

void sem_signal(semaphore_t *s){
	unsigned long flags = spin_lock_irqsave(&s->lock);

	int id = waitq_pop(s);
	if(id >= 0){
		// hand one permit to a blocked task, which will consume it on wake
		s->count++;
		tasks[id].state = TASK_READY;
	} else {
		// no waiters, increment count
		s->count++;
	}

	spin_unlock_irqrestore(&s->lock, flags);
}

int sem_trywait(semaphore_t *s){
	unsigned long flags = spin_lock_irqsave(&s->lock);

	if(s->count > 0){
		s->count--;
		spin_unlock_irqrestore(&s->lock, flags);
		return 1;
	}

	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}
