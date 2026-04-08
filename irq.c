//Hudson Strauss
#include "mmio.h"
#include "irq.h"
#include "uart.h"
#include "timer.h"

void irq_init(void){
	// disable all legacy IRQs
	IRQ_DISABLE_1 = 0xFFFFFFFF;
	IRQ_DISABLE_2 = 0xFFFFFFFF;
}

void irq_enable(int irq){
	if(irq < 32)
		IRQ_ENABLE_1 = (1 << irq);
	else
		IRQ_ENABLE_2 = (1 << (irq - 32));
}

void irq_disable(int irq){
	if(irq < 32)
		IRQ_DISABLE_1 = (1 << irq);
	else
		IRQ_DISABLE_2 = (1 << (irq - 32));
}

// called from vectors.S — check all interrupt sources and dispatch
void irq_handler(void){
	// check ARM generic timer directly via system register
	// CNTHP_CTL_EL2 bit 2 = ISTATUS (timer condition met)
	unsigned long ctl;
	__asm__ volatile("mrs %0, CNTHP_CTL_EL2" : "=r"(ctl));
	if(ctl & (1 << 2)){
		timer_irq_handler();
	}

	// check legacy controller for peripheral IRQs (UART etc)
	unsigned int pending2 = IRQ_PENDING_2;

	// UART RX interrupt (IRQ 57 = bit 25 in pending2)
	if(pending2 & (1 << (IRQ_UART - 32))){
		uart_irq_handler();
	}
}
