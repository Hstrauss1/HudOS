//Hudson Strauss
#include "sched.h"
#include "task.h"

// defined in switch.S
extern void cpu_switch_to(cpu_context_t *prev, cpu_context_t *next);

// these are in task.c but we need direct access to the array
extern task_t tasks[MAX_TASKS];
extern int current_task;
extern int num_tasks;

static volatile int preempt_pending = 0;

void schedule(void){
	// wake any sleeping tasks whose time has come
	task_wake_sleepers();

	if(num_tasks <= 1)
		return;

	int prev = current_task;
	int next = prev;

	// round-robin: find next ready task
	for(int i = 0; i < num_tasks; i++){
		next = (next + 1) % num_tasks;
		if(tasks[next].state == TASK_READY || tasks[next].state == TASK_RUNNING)
			break;
	}

	if(next == prev)
		return; // no other task to run

	// update states
	if(tasks[prev].state == TASK_RUNNING)
		tasks[prev].state = TASK_READY;
	tasks[next].state = TASK_RUNNING;
	current_task = next;

	cpu_switch_to(&tasks[prev].context, &tasks[next].context);
}

void yield(void){
	schedule();
}

// called from timer IRQ handler — just set a flag
// actual switch happens when returning to interrupted code
void sched_tick(void){
	preempt_pending = 1;
}
