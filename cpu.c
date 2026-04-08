//Hudson Strauss
#include "cpu.h"
#include "uart.h"

// defined in vectors.S
extern void exception_vector_table(void);

// CurrentEL register: bits [3:2] hold the exception level
int cpu_get_el(void){
	unsigned long el;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	return (el >> 2) & 3;
}

void cpu_install_vectors(void){
	unsigned long addr = (unsigned long)&exception_vector_table;
	__asm__ volatile("msr VBAR_EL2, %0" :: "r"(addr));
	// ensure the write completes before returning
	__asm__ volatile("isb");
}

// called from vectors.S on any exception
// type: 0=sync 1=irq 2=fiq 3=serror
// regs points to saved context on the stack:
//   [0..240]  = x0-x30
//   [248]     = ELR_EL2
//   [256]     = ESR_EL2
//   [264]     = FAR_EL2
void exception_handler(int type, unsigned long *regs){
	static const char *type_names[] = {"SYNC", "IRQ", "FIQ", "SError"};

	uart_puts("\n*** EXCEPTION: ");
	uart_puts(type_names[type & 3]);
	uart_puts(" ***\n");

	unsigned long elr = regs[248 / 8];  // ELR_EL2
	unsigned long esr = regs[256 / 8];  // ESR_EL2
	unsigned long far = regs[264 / 8];  // FAR_EL2

	uart_puts("  ELR: ");
	uart_puthex(elr);
	uart_puts("  (faulting address)\n");

	uart_puts("  ESR: ");
	uart_puthex(esr);

	// decode ESR exception class (bits [31:26])
	int ec = (esr >> 26) & 0x3F;
	uart_puts("  EC=");
	uart_puthex(ec);
	uart_puts(" -> ");
	switch(ec){
		case 0x00: uart_puts("Unknown reason"); break;
		case 0x01: uart_puts("WFI/WFE trapped"); break;
		case 0x15: uart_puts("SVC (AArch64)"); break;
		case 0x16: uart_puts("HVC (AArch64)"); break;
		case 0x18: uart_puts("MSR/MRS/System trap"); break;
		case 0x20: uart_puts("Instruction abort (lower EL)"); break;
		case 0x21: uart_puts("Instruction abort (same EL)"); break;
		case 0x22: uart_puts("PC alignment fault"); break;
		case 0x24: uart_puts("Data abort (lower EL)"); break;
		case 0x25: uart_puts("Data abort (same EL)"); break;
		case 0x26: uart_puts("SP alignment fault"); break;
		case 0x2C: uart_puts("FP exception"); break;
		case 0x3C: uart_puts("BRK (debug breakpoint)"); break;
		default:   uart_puts("Other"); break;
	}
	uart_puts("\n");

	uart_puts("  FAR: ");
	uart_puthex(far);
	uart_puts("  (fault address register)\n");

	uart_puts("SYSTEM HALTED\n");
}

void cpu_enable_irqs(void){
	__asm__ volatile("msr DAIFClr, #2"); // clear I bit
}

void cpu_disable_irqs(void){
	__asm__ volatile("msr DAIFSet, #2"); // set I bit
}
