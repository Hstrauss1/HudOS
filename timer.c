//Hudson Strauss
#include "mmio.h"
#include "timer.h"

static unsigned long timer_interval_ticks = 0;
static unsigned int timer_interval_ms = 0;
static volatile unsigned long irq_count = 0;

// --- BCM system timer (1MHz free-running, used for delay/uptime) ---

unsigned long timer_get_ticks(void){
	unsigned int hi = TIMER_CHI;
	unsigned int lo = TIMER_CLO;
	if(TIMER_CHI != hi){
		hi = TIMER_CHI;
		lo = TIMER_CLO;
	}
	return ((unsigned long)hi << 32) | lo;
}

void delay_us(unsigned long us){
	unsigned long start = timer_get_ticks();
	while(timer_get_ticks() - start < us){
	}
}

void delay_ms(unsigned long ms){
	delay_us(ms * 1000);
}

// --- ARM generic timer (hypervisor physical, for IRQs at EL2) ---

unsigned long timer_get_freq(void){
	unsigned long freq;
	__asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
	return freq;
}

// initialize and arm the periodic timer
void timer_init(unsigned int interval_ms_arg){
	timer_interval_ms = interval_ms_arg;
	unsigned long freq = timer_get_freq();
	timer_interval_ticks = (freq * timer_interval_ms) / 1000;

	// set countdown and enable the timer
	__asm__ volatile("msr CNTHP_TVAL_EL2, %0" :: "r"(timer_interval_ticks));
	__asm__ volatile("msr CNTHP_CTL_EL2, %0" :: "r"((unsigned long)1));
}

// called from irq_handler when HP timer fires
void timer_irq_handler(void){
	irq_count++;
	// re-arm for next interval
	__asm__ volatile("msr CNTHP_TVAL_EL2, %0" :: "r"(timer_interval_ticks));
}

unsigned long timer_get_irq_count(void){
	return irq_count;
}

// blocking sleep using the IRQ tick counter
void timer_sleep_ms(unsigned long ms){
	unsigned long ticks_needed = ms / timer_interval_ms;
	if(ticks_needed == 0) ticks_needed = 1;
	unsigned long target = irq_count + ticks_needed;
	while(irq_count < target){
		__asm__ volatile("wfi"); // wait for interrupt, saves power
	}
}
