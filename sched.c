//Hudson Strauss
#include "sched.h"
#include "task.h"

// defined in switch.S
extern void cpu_switch_to(cpu_context_t *prev, cpu_context_t *next);

// these are in task.c but we need direct access to the array
extern task_t tasks[MAX_TASKS];
extern int current_task;
extern int num_tasks;

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
	// After a context switch, reload TTBR0_EL1 for EL1 tasks so the
	// correct per-process page table is active before we eret back to EL1.
	{
		unsigned long ttbr = tasks[current_task].ttbr0_el1;
		if(ttbr){
			__asm__ volatile(
				"msr TTBR0_EL1, %0\n"
				"isb\n"
				"tlbi vmalle1\n"
				"dsb sy\n"
				"isb"
				:: "r"(ttbr)
			);
		}
	}
}

void yield(void){
	schedule();
}

// called from timer IRQ handler while the interrupted task's full register
// frame is already saved on its kernel stack by the vector entry path.
void sched_tick(void){
	schedule();
}
