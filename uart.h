//Hudson Strauss
#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_gets(char *buffer, int bufL);
void uart_puthex(unsigned long val);

// formatted output (supports %d %u %x %s %c %p %%)
void kprintf(const char *fmt, ...);

// output hook: fn(c) is called after every uart_putc (e.g. to mirror to fb_console)
void uart_set_output_hook(void (*fn)(char c));
void (*uart_get_output_hook(void))(char c);

// UART RX interrupt support
void uart_irq_enable(void);
void uart_irq_handler(void);
int uart_irq_getc(void);  // returns -1 if no char available
int uart_poll_getc(void); // returns -1 if no char available

#endif
