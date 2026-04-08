//Hudson Strauss
#ifndef GPIO_H
#define GPIO_H

void gpio_set_function(int pin, int func);
void gpio_write(int pin, int value);
int gpio_read(int pin);

#endif
