//Hudson Strauss
#ifndef SCHED_H
#define SCHED_H

// pick the next ready task and switch to it
void schedule(void);

// voluntarily give up the CPU
void yield(void);

// called from timer IRQ for preemptive scheduling
void sched_tick(void);

#endif
