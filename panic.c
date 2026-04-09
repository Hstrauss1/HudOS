//Hudson Strauss
#include "panic.h"
#include "uart.h"
#include "cpu.h"
#include "task.h"

extern int current_task;
extern task_t tasks[];

void panic(const char *msg){
	cpu_disable_irqs();
	kprintf("\n==============================\n");
	kprintf("*** KERNEL PANIC ***\n");
	kprintf("%s\n", msg);
	kprintf("task: %d (%s)\n", current_task, tasks[current_task].name);
	kprintf("EL: %d\n", cpu_get_el());
	kprintf("==============================\n");
	kprintf("SYSTEM HALTED\n");
	while(1) __asm__ volatile("wfe");
	__builtin_unreachable();
}

void panic_at(const char *msg, const char *file, int line){
	cpu_disable_irqs();
	kprintf("\n==============================\n");
	kprintf("*** KERNEL PANIC ***\n");
	kprintf("%s\n", msg);
	kprintf("at %s:%d\n", file, line);
	kprintf("task: %d (%s)\n", current_task, tasks[current_task].name);
	kprintf("EL: %d\n", cpu_get_el());
	kprintf("==============================\n");
	kprintf("SYSTEM HALTED\n");
	while(1) __asm__ volatile("wfe");
	__builtin_unreachable();
}
