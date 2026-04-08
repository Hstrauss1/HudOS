//Hudson Strauss
#include "string.h"
#include "uart.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"
#include "cpu.h"
#include "irq.h"
#include "alloc.h"

// skip past the current argument to the next space-separated one
static const char *next_arg(const char *s){
	while(*s && *s != ' ') ++s;
	while(*s == ' ') ++s;
	return s;
}

// --- commands ---

static void help_command(){
	uart_puts("Commands:\n");
	uart_puts("  help                show this message\n");
	uart_puts("  info                about this OS\n");
	uart_puts("  why                 why I built this\n");
	uart_puts("  clear               clear screen\n");
	uart_puts("  echo <text>         print text\n");
	uart_puts("  led <pin> <on/off>  set GPIO pin\n");
	uart_puts("  blink <pin>         blink GPIO 5 times\n");
	uart_puts("  readpin <pin>       read GPIO pin state\n");
	uart_puts("  uptime              time since boot\n");
	uart_puts("  el                  show exception level\n");
	uart_puts("  panic               trigger exception\n");
	uart_puts("  ticks               show timer IRQ count\n");
	uart_puts("  irqtest             count IRQs over 3s\n");
	uart_puts("  peek <addr>         read 32-bit value at hex address\n");
	uart_puts("  poke <addr> <val>   write 32-bit hex value to address\n");
	uart_puts("  dump <addr> [n]     dump n words (default 8)\n");
	uart_puts("  heapinfo            show allocator state\n");
	uart_puts("  malloc <size>       test allocate bytes\n");
}
static void info_command(){
	uart_puts("This is a bare metal OS coded in C and like a little assembly.\n");
}
static void why_command(){
	uart_puts("I wanted to get better at actually coding, I know most theory but have bad impl. skills\n");
}
static void clear_command(){
	uart_puts("\033[2J\033[H");
}
static void echo_command(const char *arg){
	uart_puts(arg);
	uart_puts("\n");
}

// led <pin> <on/off>
static void led_command(const char *arg){
	int pin = k_atoi(arg);
	arg = next_arg(arg);
	gpio_set_function(pin, GPIO_FUNC_OUTPUT);
	if(str_eq(arg, "on")){
		gpio_write(pin, 1);
		uart_puts("pin on\n");
	} else if(str_eq(arg, "off")){
		gpio_write(pin, 0);
		uart_puts("pin off\n");
	} else {
		uart_puts("usage: led <pin> <on/off>\n");
	}
}

// blink <pin>
static void blink_command(const char *arg){
	int pin = k_atoi(arg);
	gpio_set_function(pin, GPIO_FUNC_OUTPUT);
	uart_puts("blinking pin ");
	char buf[12];
	itoa(pin, buf, 12);
	uart_puts(buf);
	uart_puts("...\n");
	for(int i = 0; i < 5; i++){
		gpio_write(pin, 1);
		delay_ms(500);
		gpio_write(pin, 0);
		delay_ms(500);
	}
	uart_puts("done\n");
}

// readpin <pin>
static void readpin_command(const char *arg){
	int pin = k_atoi(arg);
	gpio_set_function(pin, GPIO_FUNC_INPUT);
	int val = gpio_read(pin);
	uart_puts("pin ");
	char buf[12];
	itoa(pin, buf, 12);
	uart_puts(buf);
	if(val)
		uart_puts(": HIGH\n");
	else
		uart_puts(": LOW\n");
}

static void uptime_command(){
	unsigned long ticks = timer_get_ticks();
	unsigned long secs = ticks / 1000000;
	unsigned long frac = (ticks / 1000) % 1000;
	char buf[12];
	itoa((int)secs, buf, 12);
	uart_puts(buf);
	uart_puts(".");
	if(frac < 100) uart_puts("0");
	if(frac < 10) uart_puts("0");
	itoa((int)frac, buf, 12);
	uart_puts(buf);
	uart_puts("s\n");
}

static void el_command(){
	int el = cpu_get_el();
	char buf[4];
	itoa(el, buf, 4);
	uart_puts("Current exception level: EL");
	uart_puts(buf);
	uart_puts("\n");
}

static void panic_command(){
	uart_puts("triggering exception...\n");
	__asm__ volatile("brk #1");
}

static void ticks_command(){
	unsigned long count = timer_get_irq_count();
	char buf[20];
	itoa((int)count, buf, 20);
	uart_puts("timer IRQ count: ");
	uart_puts(buf);
	uart_puts("\n");
}

static void irqtest_command(){
	uart_puts("waiting 3 seconds, counting timer IRQs...\n");
	unsigned long before = timer_get_irq_count();
	delay_ms(3000);
	unsigned long after = timer_get_irq_count();
	char buf[20];
	itoa((int)(after - before), buf, 20);
	uart_puts("timer IRQs in 3s: ");
	uart_puts(buf);
	uart_puts("\n");
}

// peek <hex_addr> - read 32-bit value
static void peek_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	uart_puts("[");
	uart_puthex(addr);
	uart_puts("] = ");
	uart_puthex(*ptr);
	uart_puts("\n");
}

// poke <hex_addr> <hex_val> - write 32-bit value
static void poke_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	arg = next_arg(arg);
	unsigned long val = k_hextoul(arg);
	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	*ptr = (unsigned int)val;
	uart_puts("[");
	uart_puthex(addr);
	uart_puts("] <- ");
	uart_puthex(val);
	uart_puts("\n");
}

// dump <hex_addr> [count] - dump count 32-bit words (default 8)
static void dump_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	arg = next_arg(arg);
	int count = 8;
	if(*arg >= '0' && *arg <= '9')
		count = k_atoi(arg);
	if(count > 64) count = 64;

	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	for(int i = 0; i < count; i++){
		if(i % 4 == 0){
			uart_puthex(addr + i * 4);
			uart_puts(": ");
		}
		uart_puthex(ptr[i]);
		uart_puts(" ");
		if(i % 4 == 3)
			uart_puts("\n");
	}
	if(count % 4 != 0)
		uart_puts("\n");
}

static void heapinfo_command(){
	char buf[20];
	uart_puts("heap used: ");
	uart_puthex(alloc_used());
	uart_puts(" (");
	itoa((int)(alloc_used() / 1024), buf, 20);
	uart_puts(buf);
	uart_puts(" KB)\n");

	uart_puts("heap free: ");
	uart_puthex(alloc_free());
	uart_puts(" (");
	itoa((int)(alloc_free() / 1024 / 1024), buf, 20);
	uart_puts(buf);
	uart_puts(" MB)\n");
}

// malloc <size> - test allocation
static void malloc_command(const char *arg){
	unsigned long size = k_hextoul(arg);
	if(size == 0) size = k_atoi(arg); // try decimal
	void *ptr = kmalloc(size);
	if(ptr){
		uart_puts("allocated ");
		char buf[20];
		itoa((int)size, buf, 20);
		uart_puts(buf);
		uart_puts(" bytes at ");
		uart_puthex((unsigned long)ptr);
		uart_puts("\n");
	} else {
		uart_puts("allocation failed (out of memory)\n");
	}
}

static void command_error(){
	uart_puts("unrecognized command\n");
}

static void check_keywords(const char *buffer){
	if(str_eq(buffer, "help")){
		help_command();
	} else if(str_eq(buffer, "info")){
		info_command();
	} else if(str_eq(buffer, "why")){
		why_command();
	} else if(str_eq(buffer, "clear")){
		clear_command();
	} else if(str_starts_with(buffer, "echo ")){
		echo_command(buffer + 5);
	} else if(str_starts_with(buffer, "led ")){
		led_command(buffer + 4);
	} else if(str_starts_with(buffer, "blink ")){
		blink_command(buffer + 6);
	} else if(str_starts_with(buffer, "readpin ")){
		readpin_command(buffer + 8);
	} else if(str_eq(buffer, "uptime")){
		uptime_command();
	} else if(str_eq(buffer, "el")){
		el_command();
	} else if(str_eq(buffer, "panic")){
		panic_command();
	} else if(str_eq(buffer, "ticks")){
		ticks_command();
	} else if(str_eq(buffer, "irqtest")){
		irqtest_command();
	} else if(str_starts_with(buffer, "peek ")){
		peek_command(buffer + 5);
	} else if(str_starts_with(buffer, "poke ")){
		poke_command(buffer + 5);
	} else if(str_starts_with(buffer, "dump ")){
		dump_command(buffer + 5);
	} else if(str_eq(buffer, "heapinfo")){
		heapinfo_command();
	} else if(str_starts_with(buffer, "malloc ")){
		malloc_command(buffer + 7);
	} else {
		command_error();
	}
}

static void query_terminal(char *terminalBuffer, int maxLen){
	while(1){
		uart_puts(">");
		uart_gets(terminalBuffer, maxLen);
		uart_puts("\n");
		check_keywords(terminalBuffer);
	}
}

void kernel_main(void) {
	int terminal_Len = 100;
	char terminal_Buffer[terminal_Len];

	cpu_install_vectors();
	uart_init();

	uart_puts("hello from bare metal\n");
	uart_puts("running at EL");
	char el_buf[4];
	itoa(cpu_get_el(), el_buf, 4);
	uart_puts(el_buf);
	uart_puts("\n");

	// set up interrupts
	irq_init();
	timer_init(1000);            // periodic timer IRQ every 1 second
	uart_irq_enable();
	irq_enable(IRQ_UART);
	cpu_enable_irqs();

	uart_puts("IRQs enabled (timer 1s, UART RX)\n");
	uart_puts("timer freq: ");
	uart_puthex(timer_get_freq());
	uart_puts(" Hz\n");

	// initialize heap
	alloc_init();
	uart_puts("heap initialized\n");

	query_terminal(terminal_Buffer, terminal_Len);

	while (1) {
	}
}
