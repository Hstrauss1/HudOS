//Hudson Strauss
#include "mmio.h"
#include "irq.h"
#include "uart.h"
#include "timer.h"
#include "sched.h"

static int irq_dispatch_iar(unsigned int iar, int used_aliased){
	unsigned int irq_id = iar & 0x3FF;

	if(irq_id == GIC_PPI_HP_TIMER){
		timer_irq_handler();
		sched_tick();
		if(used_aliased) GICC_AEOIR = iar;
		else             GICC_EOIR = iar;
		return 1;
	}

	if(irq_id < 1020){
		if(used_aliased) GICC_AEOIR = iar;
		else             GICC_EOIR = iar;
	}

#if PLATFORM_USES_MMIO_TIMER
	unsigned int pending2 = IRQ_PENDING_2;
	if(pending2 & (1 << (IRQ_UART - 32))){
		uart_irq_handler();
		return 1;
	}
#endif
	return 0;
}

void irq_init(void){
#if PLATFORM_USES_MMIO_TIMER
	// disable all legacy IRQs
	IRQ_DISABLE_1 = 0xFFFFFFFF;
	IRQ_DISABLE_2 = 0xFFFFFFFF;
#endif

	// --- GIC-400 initialization ---

	// disable distributor during config
	GICD_CTLR = 0;

	// Keep the timer PPI in Group 0. On QEMU virt at EL2 this is the route that
	// actually reaches the CPU interface reliably; our vector table handles the
	// FIQ path by dispatching through irq_handler().
	GICD_IGROUPR(0) &= ~(1U << GIC_PPI_HP_TIMER); // Group 0

	// set priority for timer PPI (lower = higher priority)
	// IPRIORITYR is byte-addressable, ID 26 is in register 26/4=6, byte 26%4=2
	unsigned int prio = GICD_IPRIORITYR(6);
	prio &= ~(0xFF << 16); // clear byte 2 (ID 26)
	prio |= (0x80 << 16);  // set to priority 0x80
	GICD_IPRIORITYR(6) = prio;

	// enable hypervisor physical timer PPI (ID 26)
	// PPIs are in ISENABLER[0] (IDs 0-31)
	GICD_ISENABLER(0) = (1 << GIC_PPI_HP_TIMER);

	// enable distributor: EnableGrp0 + EnableGrp1
	GICD_CTLR = 3;

	// enable GIC CPU interface: EnableGrp0 + EnableGrp1, FIQEn=0 (Group 0 -> IRQ)
	GICC_CTLR = 3;

	// set priority mask to accept all priorities
	GICC_PMR = 0xFF;
}

void irq_enable(int irq){
#if PLATFORM_USES_MMIO_TIMER
	if(irq < 32)
		IRQ_ENABLE_1 = (1 << irq);
	else
		IRQ_ENABLE_2 = (1 << (irq - 32));
#else
	(void)irq;
#endif
}

void irq_disable(int irq){
#if PLATFORM_USES_MMIO_TIMER
	if(irq < 32)
		IRQ_DISABLE_1 = (1 << irq);
	else
		IRQ_DISABLE_2 = (1 << (irq - 32));
#else
	(void)irq;
#endif
}

void irq_dump_gic(void){
	uart_puts("GIC state:\n");
	uart_puts("  GICD_CTLR     = "); uart_puthex(GICD_CTLR); uart_puts("\n");
	uart_puts("  GICC_CTLR     = "); uart_puthex(GICC_CTLR); uart_puts("\n");
	uart_puts("  GICC_PMR      = "); uart_puthex(GICC_PMR); uart_puts("\n");
	uart_puts("  IGROUPR(0)    = "); uart_puthex(GICD_IGROUPR(0)); uart_puts("\n");
	uart_puts("  ISENABLER(0)  = "); uart_puthex(GICD_ISENABLER(0)); uart_puts("\n");
	uart_puts("  ISPENDR(0)    = "); uart_puthex(GICD_ISPENDR(0)); uart_puts("\n");
	uart_puts("  IPRIORITYR(6) = "); uart_puthex(GICD_IPRIORITYR(6)); uart_puts("\n");

	// read timer control register
	unsigned long ctl;
	__asm__ volatile("mrs %0, CNTHP_CTL_EL2" : "=r"(ctl));
	uart_puts("  CNTHP_CTL_EL2 = "); uart_puthex(ctl);
	uart_puts(" (EN="); uart_puthex(ctl & 1);
	uart_puts(" IMASK="); uart_puthex((ctl >> 1) & 1);
	uart_puts(" ISTATUS="); uart_puthex((ctl >> 2) & 1);
	uart_puts(")\n");

	// read DAIF
	unsigned long daif;
	__asm__ volatile("mrs %0, DAIF" : "=r"(daif));
	uart_puts("  DAIF          = "); uart_puthex(daif);
	uart_puts(" (I="); uart_puthex((daif >> 7) & 1); uart_puts(")\n");
}

// called from vectors.S
void irq_handler(void){
	// read interrupt ID from GIC
	unsigned int iar = GICC_IAR;
	unsigned int irq_id = iar & 0x3FF;
	int used_aliased = 0;

	// 1022 means the highest-priority pending interrupt is in the other group.
	// On QEMU virt our timer PPI is configured as Group 1, so we must use the
	// aliased Group 1 acknowledge/end-of-interrupt registers to receive it.
	if(irq_id == 1022){
		iar = GICC_AIAR;
		used_aliased = 1;
	}
	(void)irq_id;
	(void)irq_dispatch_iar(iar, used_aliased);
}

int irq_poll(void){
	unsigned int iar = GICC_IAR;
	unsigned int irq_id = iar & 0x3FF;
	int used_aliased = 0;

	if(irq_id == 1023)
		return 0;
	if(irq_id == 1022){
		iar = GICC_AIAR;
		irq_id = iar & 0x3FF;
		used_aliased = 1;
		if(irq_id == 1023)
			return 0;
	}
	if(irq_id >= 1020)
		return 0;
	return irq_dispatch_iar(iar, used_aliased);
}
