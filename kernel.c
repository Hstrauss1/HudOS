//Hudson Strauss
//BroadcomSOC chip declarations  https://pip-assets.raspberrypi.com/categories/545-raspberry-pi-4-model-b/documents/RP-008248-DS-1-bcm2711-peripherals.pdf?disposition=inline
#define UART0_BASE 0xFE201000UL  //base
#define UART0_DR   (*(volatile unsigned int *)(UART0_BASE + 0x00)) //Data Reg I/O
#define UART0_FR   (*(volatile unsigned int *)(UART0_BASE + 0x18)) //Flag Status Reg bit4 recieve empty 5 transmit full
#define UART0_IBRD (*(volatile unsigned int *)(UART0_BASE + 0x24)) //baud rate
#define UART0_FBRD (*(volatile unsigned int *)(UART0_BASE + 0x28)) //
#define UART0_LCRH (*(volatile unsigned int *)(UART0_BASE + 0x2C)) //Line Control packet format
#define UART0_CR   (*(volatile unsigned int *)(UART0_BASE + 0x30)) //on/off switch
#define UART0_ICR  (*(volatile unsigned int *)(UART0_BASE + 0x44)) //reset interrupt state

#define spam uart_putc('1') 
#define del uart_putc('\b');



static int str_eq(const char* a, const char* b){
	while(*a && *b){
		if(*a != *b) return 0;
		++a;
		++b;
	}	
	return 1;
}



static void uart_init(void) {
	UART0_CR = 0x0;
	UART0_ICR = 0x7FF;

	UART0_IBRD = 26;
	UART0_FBRD = 3;
	UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
	UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

static void uart_putc(char c) {
	while (UART0_FR & (1 << 5)) { // Fifo in FULL (4 is in)
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

static void uart_gets(char *buffer, int bufL){
	int i = 0;

	while(i<bufL-1){
		while (UART0_FR & (1 << 4)) { // Fifo in FULL (4 is in)
		}
		buffer[i]=(char)(UART0_DR & 0xFF); //0xFF holds the input character?
		if(buffer[i]<32 || buffer[i]>127) continue; //
		if(buffer[i]=='\b'||buffer[i]==127){ //del key
			del;		
			uart_putc(' ');
			del;	
			--i;
			continue;		
		}
		if(buffer[i]=='\n'||buffer[i]=='\r'){
			break;
		} 
		uart_putc(buffer[i]);
		i++;
	}
	buffer[i+1] = '\0'; 
	return;
}


static void query_terminal(char *terminalBuffer, int maxLen){
	while(1){
		uart_puts(">");
		uart_gets(terminalBuffer, maxLen);
		uart_puts("\n");
	}
}

void kernel_main(void) {
	int terminal_Len = 100;
	char terminal_Buffer[terminal_Len];
	uart_init();
	uart_puts("hello from bare metal\n");
	query_terminal(terminal_Buffer, terminal_Len);
	uart_puts(terminal_Buffer);


	while (1) {
	}
}
