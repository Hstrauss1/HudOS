//Hudson Strauss
#ifndef MSGQUEUE_H
#define MSGQUEUE_H

#include "spinlock.h"

#define MQ_MAX_MSGS    16
#define MQ_MSG_SIZE    64  // max bytes per message

// a single message
typedef struct {
	int sender;              // task ID of sender
	int type;                // user-defined message type
	unsigned long len;       // payload length in bytes
	unsigned char data[MQ_MSG_SIZE];
} msg_t;

// message queue (circular buffer with blocking)
typedef struct {
	msg_t msgs[MQ_MAX_MSGS];
	int head;
	int tail;
	int count;
	spinlock_t lock;
	int recv_waitq[16];      // tasks blocked on receive
	int recv_head, recv_tail;
	int send_waitq[16];      // tasks blocked on send (queue full)
	int send_head, send_tail;
} msgqueue_t;

// initialize a message queue
void mq_init(msgqueue_t *q);

// send a message (blocks if queue is full)
void mq_send(msgqueue_t *q, int type, const void *data, unsigned long len);

// try to send without blocking, returns 1 on success, 0 if full
int mq_trysend(msgqueue_t *q, int type, const void *data, unsigned long len);

// receive a message (blocks if queue is empty)
// fills msg with the received message
void mq_recv(msgqueue_t *q, msg_t *msg);

// try to receive without blocking, returns 1 on success, 0 if empty
int mq_tryrecv(msgqueue_t *q, msg_t *msg);

// number of messages currently in queue
int mq_count(msgqueue_t *q);

#endif
