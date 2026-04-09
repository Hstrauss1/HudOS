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

// UART RX interrupt support
void uart_irq_enable(void);
void uart_irq_handler(void);
int uart_irq_getc(void);  // returns -1 if no char available

#endif
