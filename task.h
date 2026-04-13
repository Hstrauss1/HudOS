//Hudson Strauss
#ifndef TASK_H
#define TASK_H

#include "proc.h"

#define MAX_TASKS       16
#define TASK_STACK_SIZE 4096

// task states
#define TASK_UNUSED     0
#define TASK_READY      1
#define TASK_RUNNING    2
#define TASK_DEAD       3
#define TASK_SLEEPING   4

#define TASK_NAME_LEN   16

// callee-saved registers: x19-x30, sp
typedef struct {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long x29; // frame pointer
	unsigned long x30; // link register (return address)
	unsigned long sp;
} cpu_context_t;

typedef struct {
	int id;
	int state;
	char name[TASK_NAME_LEN];
	unsigned long wake_time;  // system timer ticks to wake at (if sleeping)
	cpu_context_t context;
	unsigned char *stack;
	unsigned long  ttbr0_el1;   // non-0 → EL1 task; loaded to TTBR0_EL1 on switch
	unsigned char *el1_stack;   // EL1 user stack allocation (freed on task exit)
	proc_t        *proc;        // EL1 page table (freed on task exit)
} task_t;

void task_init(void);
int task_create(void (*entry)(void));
int task_create_named(void (*entry)(void), const char *name);
task_t *task_current(void);
int task_count(void);

// terminate the current task and switch away
void task_exit(void);

// kill a task by ID, free its stack (cannot kill task 0)
int task_kill(int id);

// put current task to sleep for ms milliseconds, then yield
void task_sleep_ms(unsigned long ms);

// wake any sleeping tasks whose wake_time has passed (called by scheduler)
void task_wake_sleepers(void);

// internal
void task_trampoline(void);

#endif
