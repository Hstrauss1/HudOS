//Hudson Strauss
#include "user.h"
#include "task.h"
#include "alloc.h"
#include "string.h"

// stored per user-task so the wrapper knows what to call
static void (*user_entry_fn)(void);
static unsigned long user_entry_sp;

// EL2 wrapper: drops to EL1, never returns
static void user_task_wrapper(void){
	void (*fn)(void) = user_entry_fn;
	unsigned long sp = user_entry_sp;
	user_enter_el1(fn, sp);
}

int user_task_create(void (*fn)(void), const char *name){
	// allocate an EL1 stack
	unsigned char *el1_stack = (unsigned char *)kmalloc_aligned(4096, 16);
	if(!el1_stack)
		return -1;
	unsigned long el1_sp = (unsigned long)(el1_stack + 4096);
	el1_sp &= ~0xFUL;

	// stash for the wrapper (single-threaded creation, so this is safe)
	user_entry_fn = fn;
	user_entry_sp = el1_sp;

	return task_create_named(user_task_wrapper, name);
}

// drop from EL2 to EL1 and jump to fn
// sets up SPSR_EL2 for EL1h (SPx), loads ELR_EL2 with fn, eret
void user_enter_el1(void (*fn)(void), unsigned long user_sp){
	// HCR_EL2: set RW bit (bit 31) so EL1 runs in AArch64
	unsigned long hcr;
	__asm__ volatile("mrs %0, HCR_EL2" : "=r"(hcr));
	hcr |= (1UL << 31);
	__asm__ volatile("msr HCR_EL2, %0" :: "r"(hcr));

	// SPSR_EL2: return to EL1h (bits [3:0] = 0b0101 = 5)
	// D=1 A=1 I=0 F=1 (mask debug, serror, fiq; unmask irq)
	unsigned long spsr = (5UL)           // EL1h
		| (1UL << 6)                     // FIQ masked
		| (1UL << 8)                     // SError masked
		| (1UL << 9);                    // Debug masked
	__asm__ volatile("msr SPSR_EL2, %0" :: "r"(spsr));

	// ELR_EL2: address to return to
	__asm__ volatile("msr ELR_EL2, %0" :: "r"((unsigned long)fn));

	// set EL1 stack pointer
	__asm__ volatile("msr SP_EL1, %0" :: "r"(user_sp));

	// set up VBAR_EL1 to our same vector table (for EL1 exceptions)
	extern void exception_vector_table(void);
	unsigned long vbar = (unsigned long)&exception_vector_table;
	__asm__ volatile("msr VBAR_EL1, %0" :: "r"(vbar));

	// SCTLR_EL1: reset to safe defaults (MMU off for EL1 for now)
	__asm__ volatile("msr SCTLR_EL1, %0" :: "r"(0UL));

	__asm__ volatile("isb");
	__asm__ volatile("eret");
}
