//Hudson Strauss
#include "mmio.h"
#include "gpio.h"

// each GPFSEL register covers 10 pins, 3 bits per pin
// GPFSEL0 = pins 0-9, GPFSEL1 = pins 10-19, etc.
static volatile unsigned int *gpfsel_reg(int pin){
	return (volatile unsigned int *)(GPIO_BASE + 0x00 + (pin / 10) * 4);
}

void gpio_set_function(int pin, int func){
	if(pin < 0 || pin > 57) return;
	volatile unsigned int *reg = gpfsel_reg(pin);
	int shift = (pin % 10) * 3;
	unsigned int val = *reg;
	val &= ~(7 << shift);       //clear 3 bits
	val |= (func & 7) << shift; //set new function
	*reg = val;
}

void gpio_write(int pin, int value){
	if(pin < 0 || pin > 57) return;
	if(pin < 32){
		if(value)
			GPSET0 = (1 << pin);
		else
			GPCLR0 = (1 << pin);
	} else {
		if(value)
			GPSET1 = (1 << (pin - 32));
		else
			GPCLR1 = (1 << (pin - 32));
	}
}

int gpio_read(int pin){
	if(pin < 0 || pin > 57) return 0;
	if(pin < 32)
		return (GPLEV0 >> pin) & 1;
	else
		return (GPLEV1 >> (pin - 32)) & 1;
}
