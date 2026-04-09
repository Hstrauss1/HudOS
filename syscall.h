//Hudson Strauss
#ifndef SYSCALL_H
#define SYSCALL_H

// syscall numbers
#define SYS_PUTC    0
#define SYS_PUTS    1
#define SYS_TICKS   2
#define SYS_YIELD   3
#define SYS_EXIT    4
#define SYS_SLEEP   5

// kernel-side dispatcher (called from exception handler)
unsigned long syscall_handler(unsigned long num, unsigned long arg0, unsigned long arg1, unsigned long arg2);

// user-side wrappers (inline, use SVC)
static inline void sys_putc(char c){
	__asm__ volatile(
		"mov x0, %0\n"
		"mov x8, %1\n"
		"svc #0\n"
		:: "r"((unsigned long)c), "r"((unsigned long)SYS_PUTC)
		: "x0", "x8"
	);
}

static inline void sys_puts(const char *s){
	__asm__ volatile(
		"mov x0, %0\n"
		"mov x8, %1\n"
		"svc #0\n"
		:: "r"((unsigned long)s), "r"((unsigned long)SYS_PUTS)
		: "x0", "x8"
	);
}

static inline unsigned long sys_ticks(void){
	unsigned long ret;
	__asm__ volatile(
		"mov x8, %1\n"
		"svc #0\n"
		"mov %0, x0\n"
		: "=r"(ret) : "r"((unsigned long)SYS_TICKS)
		: "x0", "x8"
	);
	return ret;
}

static inline void sys_yield(void){
	__asm__ volatile(
		"mov x8, %0\n"
		"svc #0\n"
		:: "r"((unsigned long)SYS_YIELD)
		: "x8"
	);
}

static inline void sys_exit(void){
	__asm__ volatile(
		"mov x8, %0\n"
		"svc #0\n"
		:: "r"((unsigned long)SYS_EXIT)
		: "x8"
	);
}

static inline void sys_sleep(unsigned long ms){
	__asm__ volatile(
		"mov x0, %0\n"
		"mov x8, %1\n"
		"svc #0\n"
		:: "r"(ms), "r"((unsigned long)SYS_SLEEP)
		: "x0", "x8"
	);
}

#endif
