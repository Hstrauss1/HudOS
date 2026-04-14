//Hudson Strauss
#ifndef TIMER_H
#define TIMER_H

// system timer (free-running 1MHz counter)
unsigned long timer_get_ticks(void);
void delay_us(unsigned long us);
void delay_ms(unsigned long ms);
unsigned long timer_clock_hz(void);
unsigned long timer_ms_to_ticks(unsigned long ms);

// ARM generic timer IRQ support (hypervisor physical timer at EL2)
void timer_init(unsigned int interval_ms);
void timer_irq_handler(void);
unsigned long timer_get_irq_count(void);
unsigned long timer_get_freq(void);

// sleep using IRQ-based tick counting (requires IRQs enabled)
void timer_sleep_ms(unsigned long ms);

#endif
