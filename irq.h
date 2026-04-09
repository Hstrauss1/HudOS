//Hudson Strauss
#ifndef IRQ_H
#define IRQ_H

// IRQ numbers in the BCM legacy controller
// IRQ_PENDING_1 / IRQ_ENABLE_1 bits (GPU IRQs 0-31)
#define IRQ_TIMER_1     1   // system timer compare 1
#define IRQ_TIMER_3     3   // system timer compare 3
#define IRQ_UART        57  // PL011 UART (in IRQ_PENDING_2, bit 25)

// initialize the interrupt controller (disable all)
void irq_init(void);

// enable a specific IRQ number
void irq_enable(int irq);

// disable a specific IRQ number
void irq_disable(int irq);

// called from vectors.S on IRQ entry - dispatches to registered handlers
void irq_handler(void);

// dump GIC and timer state for debugging
void irq_dump_gic(void);

#endif
