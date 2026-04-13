//Hudson Strauss
#include "task.h"
#include "alloc.h"
#include "string.h"
#include "timer.h"
#include "proc.h"

task_t tasks[MAX_TASKS];
int current_task = 0;
int num_tasks = 0;

void task_init(void){
	memset(tasks, 0, sizeof(tasks));

	// task 0 = the kernel main thread (already running)
	tasks[0].id = 0;
	tasks[0].state = TASK_RUNNING;
	tasks[0].stack = 0; // uses the boot stack
	// copy name safely
	const char *kname = "kernel";
	int k = 0;
	while(kname[k] && k < TASK_NAME_LEN - 1){ tasks[0].name[k] = kname[k]; k++; }
	tasks[0].name[k] = '\0';
	current_task = 0;
	num_tasks = 1;
}

int task_create_named(void (*entry)(void), const char *name){
	// find a free slot (reuse dead/unused slots, or append)
	int id = -1;
	for(int i = 1; i < MAX_TASKS; i++){
		if(tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD){
			// Free EL2 stack from a dead slot so it isn't leaked.
			if(tasks[i].state == TASK_DEAD && tasks[i].stack){
				kfree(tasks[i].stack);
				tasks[i].stack = 0;
			}
			id = i;
			break;
		}
	}
	if(id < 0)
		return -1;

	task_t *t = &tasks[id];

	t->id = id;
	t->state = TASK_READY;
	t->wake_time = 0;
	t->ttbr0_el1 = 0;
	t->el1_stack  = 0;
	t->proc       = 0;

	// copy name
	int k = 0;
	if(name){
		while(name[k] && k < TASK_NAME_LEN - 1){ t->name[k] = name[k]; k++; }
	}
	t->name[k] = '\0';

	// allocate a stack
	t->stack = (unsigned char *)kmalloc_aligned(TASK_STACK_SIZE, 16);
	if(!t->stack)
		return -1;

	// stack grows down — point to top, 16-byte aligned
	unsigned long sp = (unsigned long)(t->stack + TASK_STACK_SIZE);
	sp &= ~0xFUL;

	memset(&t->context, 0, sizeof(cpu_context_t));
	t->context.x19 = (unsigned long)entry;           // real entry in x19
	t->context.x30 = (unsigned long)task_trampoline;  // lr = trampoline
	t->context.sp = sp;

	if(id >= num_tasks)
		num_tasks = id + 1;
	return id;
}

int task_create(void (*entry)(void)){
	return task_create_named(entry, "task");
}

// entry point for new tasks — x19 holds the real function pointer
void task_trampoline(void){
	void (*entry)(void);
	__asm__ volatile("mov %0, x19" : "=r"(entry));
	entry();
	// if entry returns, terminate the task
	task_exit();
}

static void task_cleanup(task_t *t){
	t->state = TASK_DEAD;
	if(t->stack){
		kfree(t->stack);
		t->stack = 0;
	}
	if(t->el1_stack){ kfree(t->el1_stack); t->el1_stack = 0; }
	if(t->proc)     { proc_free(t->proc);  t->proc = 0;       }
	t->ttbr0_el1 = 0;
}

void task_exit(void){
	extern void schedule(void);
	task_cleanup(&tasks[current_task]);
	schedule();
	// should never reach here, but just in case
	while(1) __asm__ volatile("wfe");
}

int task_kill(int id){
	// cannot kill task 0 (kernel shell) or invalid IDs
	if(id <= 0 || id >= num_tasks)
		return -1;
	if(tasks[id].state == TASK_UNUSED || tasks[id].state == TASK_DEAD)
		return -1;

	task_cleanup(&tasks[id]);
	// note: if killing the current task, we need to reschedule
	if(id == current_task){
		extern void schedule(void);
		schedule();
	}
	return 0;
}

void task_sleep_ms(unsigned long ms){
	extern void schedule(void);
	tasks[current_task].state = TASK_SLEEPING;
	tasks[current_task].wake_time = timer_get_ticks() + (ms * 1000); // ticks are microseconds
	schedule();
}

void task_wake_sleepers(void){
	unsigned long now = timer_get_ticks();
	for(int i = 0; i < num_tasks; i++){
		if(tasks[i].state == TASK_SLEEPING && now >= tasks[i].wake_time){
			tasks[i].state = TASK_READY;
			tasks[i].wake_time = 0;
		}
	}
}

task_t *task_current(void){
	return &tasks[current_task];
}

int task_count(void){
	return num_tasks;
}
