//Hudson Strauss
#include "cpu.h"
#include "uart.h"
#include "task.h"

// defined in vectors.S
extern void exception_vector_table(void);

extern int current_task;
extern task_t tasks[];

// CurrentEL register: bits [3:2] hold the exception level
int cpu_get_el(void){
	unsigned long el;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	return (el >> 2) & 3;
}

void cpu_install_vectors(void){
	unsigned long addr = (unsigned long)&exception_vector_table;
	__asm__ volatile("msr VBAR_EL2, %0" :: "r"(addr));
	__asm__ volatile("isb");
}

// decode data abort ISS (instruction-specific syndrome)
static void decode_data_abort(unsigned long esr){
	int wnr = (esr >> 6) & 1;
	int dfsc = esr & 0x3F;

	kprintf("  access: %s\n", wnr ? "WRITE" : "READ");
	kprintf("  DFSC: %x -> ", dfsc);

	switch(dfsc & 0x3C){
		case 0x00:
			switch(dfsc & 0x03){
				case 0: kprintf("address size fault L0\n"); break;
				case 1: kprintf("address size fault L1\n"); break;
				case 2: kprintf("address size fault L2\n"); break;
				case 3: kprintf("address size fault L3\n"); break;
			}
			break;
		case 0x04:
			kprintf("translation fault L%d\n", dfsc & 0x03);
			break;
		case 0x08:
			kprintf("access flag fault L%d\n", dfsc & 0x03);
			break;
		case 0x0C:
			kprintf("permission fault L%d\n", dfsc & 0x03);
			break;
		case 0x10:
			kprintf("synchronous external abort\n");
			break;
		case 0x14:
			kprintf("synchronous parity/ECC error\n");
			break;
		case 0x18:
			kprintf("synchronous external abort (on walk)\n");
			break;
		case 0x20:
			kprintf("TLB conflict abort\n");
			break;
		case 0x30:
			kprintf("section domain fault\n");
			break;
		case 0x34:
			kprintf("page domain fault\n");
			break;
		default:
			kprintf("unknown (0x%x)\n", dfsc);
			break;
	}

	if(esr & (1 << 24))
		kprintf("  ISV: syndrome valid, SAS=%d\n", (int)((esr >> 22) & 3));
}

// dump all saved registers
static void dump_registers(unsigned long *regs){
	kprintf("\n  --- Register Dump ---\n");
	for(int i = 0; i < 31; i += 2){
		kprintf("  x%-2d: %x", i, regs[i]);
		if(i + 1 < 31)
			kprintf("    x%-2d: %x", i + 1, regs[i + 1]);
		kprintf("\n");
	}
}

// walk the frame pointer chain and print return addresses
static void dump_stack(unsigned long fp, unsigned long sp){
	kprintf("\n  --- Stack Trace ---\n");
	kprintf("  SP: %x\n", sp);

	// walk frame pointer chain (x29 = fp, x30 = lr at [fp+8])
	int depth = 0;
	while(fp && depth < 16){
		unsigned long *frame = (unsigned long *)fp;
		unsigned long lr = frame[1]; // return address
		if(lr == 0) break;
		kprintf("  #%d  %x\n", depth, lr);
		fp = frame[0]; // previous frame pointer
		depth++;
	}
	if(depth == 0)
		kprintf("  (no frame pointer chain available)\n");

	// also dump raw stack words
	kprintf("\n  --- Stack Dump (16 words from SP) ---\n");
	unsigned long *stack = (unsigned long *)sp;
	for(int i = 0; i < 16; i += 2){
		kprintf("  [SP+%x]: %x  %x\n",
			i * 8, stack[i], stack[i + 1]);
	}
}

// called from vectors.S on any fatal exception
// type: 0=sync 1=irq 2=fiq 3=serror
// regs points to saved context on the stack:
//   [0..240]  = x0-x30  (31 registers * 8 bytes)
//   [248]     = ELR_EL2
//   [256]     = ESR_EL2
//   [264]     = FAR_EL2
void exception_handler(int type, unsigned long *regs){
	static const char *type_names[] = {"SYNC", "IRQ", "FIQ", "SError"};

	cpu_disable_irqs();

	kprintf("\n============================================\n");
	kprintf("*** EXCEPTION: %s ***\n", type_names[type & 3]);
	kprintf("============================================\n");

	unsigned long elr = regs[248 / 8];
	unsigned long esr = regs[256 / 8];
	unsigned long far = regs[264 / 8];
	int ec = (esr >> 26) & 0x3F;

	kprintf("  ELR: %x  (faulting instruction)\n", elr);
	kprintf("  ESR: %x\n", esr);
	kprintf("  FAR: %x  (fault address)\n", far);

	// decode exception class
	kprintf("  EC:  %x -> ", ec);
	switch(ec){
		case 0x00: kprintf("Unknown reason\n"); break;
		case 0x01: kprintf("WFI/WFE trapped\n"); break;
		case 0x0E: kprintf("Illegal execution state\n"); break;
		case 0x15: kprintf("SVC (AArch64)\n"); break;
		case 0x16: kprintf("HVC (AArch64)\n"); break;
		case 0x17: kprintf("SMC (AArch64)\n"); break;
		case 0x18: kprintf("MSR/MRS/System trap\n"); break;
		case 0x20: kprintf("Instruction abort (lower EL)\n"); break;
		case 0x21: kprintf("Instruction abort (same EL)\n"); break;
		case 0x22: kprintf("PC alignment fault\n"); break;
		case 0x24: kprintf("Data abort (lower EL)\n"); break;
		case 0x25: kprintf("Data abort (same EL)\n"); break;
		case 0x26: kprintf("SP alignment fault\n"); break;
		case 0x28: kprintf("FP exception (AArch32)\n"); break;
		case 0x2C: kprintf("FP exception (AArch64)\n"); break;
		case 0x2F: kprintf("SError interrupt\n"); break;
		case 0x30: kprintf("Breakpoint (lower EL)\n"); break;
		case 0x31: kprintf("Breakpoint (same EL)\n"); break;
		case 0x32: kprintf("Software step (lower EL)\n"); break;
		case 0x33: kprintf("Software step (same EL)\n"); break;
		case 0x34: kprintf("Watchpoint (lower EL)\n"); break;
		case 0x35: kprintf("Watchpoint (same EL)\n"); break;
		case 0x38: kprintf("BKPT (AArch32)\n"); break;
		case 0x3C: kprintf("BRK (AArch64)\n"); break;
		default:   kprintf("Other (0x%x)\n", ec); break;
	}

	// extra decoding for data aborts
	if(ec == 0x24 || ec == 0x25)
		decode_data_abort(esr);

	// extra decoding for instruction aborts
	if(ec == 0x20 || ec == 0x21){
		int ifsc = esr & 0x3F;
		kprintf("  IFSC: %x -> ", ifsc);
		switch(ifsc & 0x3C){
			case 0x04: kprintf("translation fault L%d\n", ifsc & 3); break;
			case 0x08: kprintf("access flag fault L%d\n", ifsc & 3); break;
			case 0x0C: kprintf("permission fault L%d\n", ifsc & 3); break;
			default:   kprintf("other (0x%x)\n", ifsc); break;
		}
	}

	// task info
	kprintf("\n  task: %d (%s)\n", current_task, tasks[current_task].name);
	kprintf("  EL:   %d\n", cpu_get_el());

	// full register dump
	dump_registers(regs);

	// stack trace using x29 (frame pointer) and SP
	unsigned long fp = regs[29];  // x29
	// SP was decremented by 272 for SAVE_CONTEXT, original SP = current + 272
	unsigned long sp = (unsigned long)regs + 272;
	dump_stack(fp, sp);

	kprintf("\n============================================\n");
	kprintf("SYSTEM HALTED\n");
	kprintf("============================================\n");
}

void cpu_enable_irqs(void){
	__asm__ volatile("msr DAIFClr, #2");
}

void cpu_disable_irqs(void){
	__asm__ volatile("msr DAIFSet, #2");
}
