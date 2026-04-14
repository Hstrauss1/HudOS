//Hudson Strauss
#include "mmio.h"
#include "uart.h"
#include "platform.h"

#define del uart_putc('\b');

// simple ring buffer for IRQ-received characters
#define UART_BUF_SIZE 64
static volatile char uart_rx_buf[UART_BUF_SIZE];
static volatile int uart_rx_head = 0;
static volatile int uart_rx_tail = 0;

// optional hook: called from uart_putc for every output character
// use this to mirror all kernel output to a second destination (e.g. fb_console)
static void (*uart_output_hook)(char c) = 0;

void uart_set_output_hook(void (*fn)(char c)){
	uart_output_hook = fn;
}

void (*uart_get_output_hook(void))(char c){
	return uart_output_hook;
}

void uart_init(void) {
#if defined(PLATFORM_VIRT)
	UART0_ICR = 0x7FF;
	UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
	return;
#endif
	UART0_CR = 0x0;
	UART0_ICR = 0x7FF;

	UART0_IBRD = PLATFORM_UART_IBRD;
	UART0_FBRD = PLATFORM_UART_FBRD;
	UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
	UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(char c) {
	while (UART0_FR & (1 << 5)) {}
	UART0_DR = (unsigned int)c;
	if(uart_output_hook) uart_output_hook(c);
}

void uart_puts(const char *s) {
	while (*s) {
		if (*s == '\n') {
			uart_putc('\r');
		}
		uart_putc(*s++);
	}
}

void uart_gets(char *buffer, int bufL){
	int i = 0;

	while(i<bufL-1){
		while (UART0_FR & (1 << 4)) { // Fifo in FULL (4 is in)
		}
		buffer[i]=(char)(UART0_DR & 0xFF); //0xFF holds the input character?
		if(buffer[i]=='\b'||buffer[i]==127){ //del key
			if(i > 0){
				del;
				uart_putc(' ');
				del;
				--i;
			}
			continue;
		}
		if(buffer[i]=='\n'||buffer[i]=='\r'){
			break;
		}
		if(buffer[i]<32 || buffer[i]>127) continue; //ignoring other chars.
		uart_putc(buffer[i]);
		i++;
	}
	buffer[i] = '\0';
	return;
}

void uart_puthex(unsigned long val){
	uart_putc('0');
	uart_putc('x');
	int started = 0;
	for(int shift = 60; shift >= 0; shift -= 4){
		int nibble = (val >> shift) & 0xF;
		if(nibble || started || shift == 0){
			started = 1;
			uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
		}
	}
}

// --- kprintf: formatted output ---

// helper: print unsigned decimal
static void print_unsigned(unsigned long val){
	char buf[20];
	int i = 0;
	if(val == 0){
		uart_putc('0');
		return;
	}
	while(val > 0){
		buf[i++] = '0' + (val % 10);
		val /= 10;
	}
	while(i > 0) uart_putc(buf[--i]);
}

// helper: print signed decimal
static void print_signed(long val){
	if(val < 0){
		uart_putc('-');
		val = -val;
	}
	print_unsigned((unsigned long)val);
}

// helper: print hex with 0x prefix
static void print_hex(unsigned long val){
	uart_putc('0');
	uart_putc('x');
	int started = 0;
	for(int shift = 60; shift >= 0; shift -= 4){
		int nibble = (val >> shift) & 0xF;
		if(nibble || started || shift == 0){
			started = 1;
			uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
		}
	}
}

// minimal variadic printf using __builtin_va_*
void kprintf(const char *fmt, ...){
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);

	while(*fmt){
		if(*fmt != '%'){
			if(*fmt == '\n') uart_putc('\r');
			uart_putc(*fmt++);
			continue;
		}
		fmt++; // skip '%'
		switch(*fmt){
			case 'd': {
				long val = __builtin_va_arg(ap, long);
				print_signed(val);
				break;
			}
			case 'u': {
				unsigned long val = __builtin_va_arg(ap, unsigned long);
				print_unsigned(val);
				break;
			}
			case 'x': {
				unsigned long val = __builtin_va_arg(ap, unsigned long);
				print_hex(val);
				break;
			}
			case 'p': {
				void *val = __builtin_va_arg(ap, void *);
				print_hex((unsigned long)val);
				break;
			}
			case 's': {
				const char *s = __builtin_va_arg(ap, const char *);
				if(!s) s = "(null)";
				uart_puts(s);
				break;
			}
			case 'c': {
				int c = __builtin_va_arg(ap, int);
				uart_putc((char)c);
				break;
			}
			case '%':
				uart_putc('%');
				break;
			default:
				uart_putc('%');
				uart_putc(*fmt);
				break;
		}
		fmt++;
	}

	__builtin_va_end(ap);
}

// enable UART RX interrupt (bit 4 = RXIM)
void uart_irq_enable(void){
	UART0_IMSC |= (1 << 4);
}

// called from irq_handler when UART interrupt fires
void uart_irq_handler(void){
	// drain the FIFO into the ring buffer
	while(!(UART0_FR & (1 << 4))){
		char c = (char)(UART0_DR & 0xFF);
		int next = (uart_rx_head + 1) % UART_BUF_SIZE;
		if(next != uart_rx_tail){ // not full
			uart_rx_buf[uart_rx_head] = c;
			uart_rx_head = next;
		}
	}
	// clear the RX interrupt
	UART0_ICR = (1 << 4);
}

// returns next char from IRQ ring buffer, or -1 if empty
int uart_irq_getc(void){
	if(uart_rx_head == uart_rx_tail)
		return -1;
	char c = uart_rx_buf[uart_rx_tail];
	uart_rx_tail = (uart_rx_tail + 1) % UART_BUF_SIZE;
	return c;
}

// polled receive path for environments where UART RX interrupts are unreliable
int uart_poll_getc(void){
	if(UART0_FR & (1 << 4))
		return -1;
	return (int)(UART0_DR & 0xFF);
}
