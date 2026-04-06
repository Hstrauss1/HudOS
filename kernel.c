//Hudson Strauss
//BroadcomSOC chip declarations
#define UART0_BASE 0xFE201000UL  //base
#define UART0_DR   (*(volatile unsigned int *)(UART0_BASE + 0x00)) //Data Reg I/O
#define UART0_FR   (*(volatile unsigned int *)(UART0_BASE + 0x18)) //Flag Status Reg bit4 recieve empty 5 transmit full
#define UART0_IBRD (*(volatile unsigned int *)(UART0_BASE + 0x24))
#define UART0_FBRD (*(volatile unsigned int *)(UART0_BASE + 0x28))
#define UART0_LCRH (*(volatile unsigned int *)(UART0_BASE + 0x2C))
#define UART0_CR   (*(volatile unsigned int *)(UART0_BASE + 0x30))
#define UART0_ICR  (*(volatile unsigned int *)(UART0_BASE + 0x44))

static void uart_init(void) {
    UART0_CR = 0x0;
    UART0_ICR = 0x7FF;

    UART0_IBRD = 26;
    UART0_FBRD = 3;
    UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
    UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

static void uart_putc(char c) {
    while (UART0_FR & (1 << 5)) {
    }
    UART0_DR = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void kernel_main(void) {
    uart_init();
    uart_puts("hello from bare metal\n");

    while (1) {
    }
}