//Hudson Strauss
#include "msgqueue.h"
#include "task.h"
#include "sched.h"
#include "string.h"

extern task_t tasks[];
extern int current_task;

void mq_init(msgqueue_t *q){
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	spin_init(&q->lock);
	q->recv_head = q->recv_tail = 0;
	q->send_head = q->send_tail = 0;
}

// --- internal wait queue helpers ---

static int waitq_push(int *wq, int *head, int *tail, int id){
	int next = (*head + 1) % 16;
	if(next == *tail)
		return -1;
	wq[*head] = id;
	*head = next;
	return 0;
}

static int waitq_pop(int *wq, int *head, int *tail){
	if(*tail == *head)
		return -1;
	int id = wq[*tail];
	*tail = (*tail + 1) % 16;
	return id;
}

// --- send ---

void mq_send(msgqueue_t *q, int type, const void *data, unsigned long len){
	if(len > MQ_MSG_SIZE) len = MQ_MSG_SIZE;

	while(1){
		unsigned long flags = spin_lock_irqsave(&q->lock);

		if(q->count < MQ_MAX_MSGS){
			// room available — enqueue
			msg_t *m = &q->msgs[q->head];
			m->sender = current_task;
			m->type = type;
			m->len = len;
			if(len > 0) memcpy(m->data, data, len);
			q->head = (q->head + 1) % MQ_MAX_MSGS;
			q->count++;

			// wake a receiver if one is waiting
			int id = waitq_pop(q->recv_waitq, &q->recv_head, &q->recv_tail);
			if(id >= 0) tasks[id].state = TASK_READY;

			spin_unlock_irqrestore(&q->lock, flags);
			return;
		}

		// queue full — block
		int me = current_task;
		if(waitq_push(q->send_waitq, &q->send_head, &q->send_tail, me) == 0){
			tasks[me].state = TASK_BLOCKED;
			tasks[me].wake_time = 0;
		}
		spin_unlock_irqrestore(&q->lock, flags);
		schedule();
	}
}

int mq_trysend(msgqueue_t *q, int type, const void *data, unsigned long len){
	if(len > MQ_MSG_SIZE) len = MQ_MSG_SIZE;

	unsigned long flags = spin_lock_irqsave(&q->lock);

	if(q->count >= MQ_MAX_MSGS){
		spin_unlock_irqrestore(&q->lock, flags);
		return 0;
	}

	msg_t *m = &q->msgs[q->head];
	m->sender = current_task;
	m->type = type;
	m->len = len;
	if(len > 0) memcpy(m->data, data, len);
	q->head = (q->head + 1) % MQ_MAX_MSGS;
	q->count++;

	int id = waitq_pop(q->recv_waitq, &q->recv_head, &q->recv_tail);
	if(id >= 0) tasks[id].state = TASK_READY;

	spin_unlock_irqrestore(&q->lock, flags);
	return 1;
}

// --- receive ---

void mq_recv(msgqueue_t *q, msg_t *msg){
	while(1){
		unsigned long flags = spin_lock_irqsave(&q->lock);

		if(q->count > 0){
			// message available — dequeue
			msg_t *m = &q->msgs[q->tail];
			msg->sender = m->sender;
			msg->type = m->type;
			msg->len = m->len;
			if(m->len > 0) memcpy(msg->data, m->data, m->len);
			q->tail = (q->tail + 1) % MQ_MAX_MSGS;
			q->count--;

			// wake a sender if one is blocked
			int id = waitq_pop(q->send_waitq, &q->send_head, &q->send_tail);
			if(id >= 0) tasks[id].state = TASK_READY;

			spin_unlock_irqrestore(&q->lock, flags);
			return;
		}

		// queue empty — block
		int me = current_task;
		if(waitq_push(q->recv_waitq, &q->recv_head, &q->recv_tail, me) == 0){
			tasks[me].state = TASK_BLOCKED;
			tasks[me].wake_time = 0;
		}
		spin_unlock_irqrestore(&q->lock, flags);
		schedule();
	}
}

int mq_tryrecv(msgqueue_t *q, msg_t *msg){
	unsigned long flags = spin_lock_irqsave(&q->lock);

	if(q->count == 0){
		spin_unlock_irqrestore(&q->lock, flags);
		return 0;
	}

	msg_t *m = &q->msgs[q->tail];
	msg->sender = m->sender;
	msg->type = m->type;
	msg->len = m->len;
	if(m->len > 0) memcpy(msg->data, m->data, m->len);
	q->tail = (q->tail + 1) % MQ_MAX_MSGS;
	q->count--;

	int id = waitq_pop(q->send_waitq, &q->send_head, &q->send_tail);
	if(id >= 0) tasks[id].state = TASK_READY;

	spin_unlock_irqrestore(&q->lock, flags);
	return 1;
}

int mq_count(msgqueue_t *q){
	unsigned long flags = spin_lock_irqsave(&q->lock);
	int c = q->count;
	spin_unlock_irqrestore(&q->lock, flags);
	return c;
}
