//Hudson Strauss

//Refer to mmio.h for pin declarations
#include "string.h"
#include "uart.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"
#include "cpu.h"
#include "irq.h"
#include "alloc.h"
#include "mmu.h"
#include "task.h"
#include "sched.h"
#include "ramfs.h"
#include "spinlock.h"
#include "semaphore.h"
#include "fb.h"
#include "mutex.h"
#include "msgqueue.h"
#include "panic.h"
#include "user.h"
#include "syscall.h"
#include "version.h"
#include "test.h"
#include "home.h"

// skip past the current argument to the next space-separated one
static const char *next_arg(const char *s){
	while(*s && *s != ' ') ++s;
	while(*s == ' ') ++s;
	return s;
}

// --- commands ---

static void help_command(){
	uart_puts("Commands:\n");
	uart_puts("\n[system]\n");
	uart_puts("  help                show this message\n");
	uart_puts("  info                version and build info\n");
	uart_puts("  why                 why I built this\n");
	uart_puts("  clear               clear screen\n");
	uart_puts("  echo <text>         print text\n");
	uart_puts("  el                  show exception level\n");
	uart_puts("  uptime              time since boot\n");
	uart_puts("  panic               trigger kernel panic\n");
	uart_puts("  crashtest <type>    null/assert/brk/undef/align\n");
	uart_puts("\n[gpio]\n");
	uart_puts("  led <pin> <on/off>  set GPIO pin output\n");
	uart_puts("  blink <pin>         blink GPIO pin 5 times\n");
	uart_puts("  readpin <pin>       read GPIO pin state\n");
	uart_puts("\n[timer/irq]\n");
	uart_puts("  ticks               show timer IRQ count\n");
	uart_puts("  irqtest             count IRQs over 3s\n");
	uart_puts("  timerdbg            dump GIC/timer state\n");
	uart_puts("\n[memory]\n");
	uart_puts("  peek <addr>         read 32-bit value at hex address\n");
	uart_puts("  poke <addr> <val>   write 32-bit hex value to address\n");
	uart_puts("  dump <addr> [n]     dump n words (default 8)\n");
	uart_puts("  heapinfo            show allocator state\n");
	uart_puts("  malloc <size>       test allocate bytes\n");
	uart_puts("  free <addr>         free allocated memory\n");
	uart_puts("\n[tasks]\n");
	uart_puts("  tasks               list running tasks\n");
	uart_puts("  spawn               spawn a background counter task\n");
	uart_puts("  kill <id>           kill a task by ID\n");
	uart_puts("  sleep <ms>          sleep current task for ms\n");
	uart_puts("  yield               yield to next task\n");
	uart_puts("  uspawn              spawn a user-mode demo task (EL1)\n");
	uart_puts("\n[ipc/sync]\n");
	uart_puts("  locktest            test spinlock primitives\n");
	uart_puts("  semtest             producer/consumer semaphore demo\n");
	uart_puts("  mqtest              message queue ping-pong demo\n");
	uart_puts("  mutextest           mutex shared counter demo\n");
	uart_puts("\n[filesystem]\n");
	uart_puts("  mkfile <name>       create a ramfs file\n");
	uart_puts("  write <name> <data> write data to file\n");
	uart_puts("  cat <name>          read file contents\n");
	uart_puts("  ls                  list ramfs files\n");
	uart_puts("\n[framebuffer]\n");
	uart_puts("  fbtest              draw test pattern on framebuffer\n");
	uart_puts("  fbmirror            toggle horizontal mirror (QEMU fix)\n");
	uart_puts("  home                launch live desktop (auto-updates)\n");
	uart_puts("  home stop           close the desktop\n");
	uart_puts("\n[self-tests]\n");
	uart_puts("  test_uart           test UART and string library\n");
	uart_puts("  test_gpio           test GPIO driver\n");
	uart_puts("  test_timer          test timer driver\n");
	uart_puts("  test_alloc          test memory allocator\n");
	uart_puts("  test_all            run all self-tests\n");
}
static void info_command(){
	kprintf("%s v%s\n", HUDOS_NAME, HUDOS_VERSION);
	kprintf("Arch:   %s running at %s\n", HUDOS_ARCH, HUDOS_EL);
	kprintf("Board:  %s\n", HUDOS_BOARD);
	kprintf("Author: %s\n", HUDOS_AUTHOR);
	kprintf("Built:  %s %s\n", __DATE__, __TIME__);
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
	kprintf("Current exception level: EL%d\n", cpu_get_el());
}

static void panic_command(){
	panic("user triggered panic from shell");
}

static void crashtest_command(const char *arg){
	if(str_eq(arg, "null")){
		kprintf("dereferencing NULL pointer...\n");
		volatile int *p = (volatile int *)0;
		*p = 42;
	} else if(str_eq(arg, "assert")){
		kprintf("triggering failed assert...\n");
		ASSERT(1 == 2);
	} else if(str_eq(arg, "brk")){
		kprintf("triggering BRK...\n");
		__asm__ volatile("brk #1");
	} else if(str_eq(arg, "undef")){
		kprintf("executing undefined instruction...\n");
		__asm__ volatile(".word 0x00000000");
	} else if(str_eq(arg, "align")){
		kprintf("triggering unaligned access...\n");
		volatile unsigned long *p = (volatile unsigned long *)0x80003;
		(void)*p;
	} else {
		kprintf("usage: crashtest <null|assert|brk|undef|align>\n");
	}
}

static void ticks_command(){
	kprintf("timer IRQ count: %d\n", timer_get_irq_count());
}

static void irqtest_command(){
	kprintf("waiting 3 seconds, counting timer IRQs...\n");
	unsigned long before = timer_get_irq_count();
	delay_ms(3000);
	unsigned long after = timer_get_irq_count();
	kprintf("timer IRQs in 3s: %d\n", after - before);
}

// peek <hex_addr>
static void peek_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	volatile unsigned int *ptr = (volatile unsigned int *)addr;
	uart_puts("[");
	uart_puthex(addr);
	uart_puts("] = ");
	uart_puthex(*ptr);
	uart_puts("\n");
}

// poke <hex_addr> <hex_val>
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

// dump <hex_addr> [count]
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
	kprintf("heap used: %x (%d KB)\n", alloc_used(), alloc_used() / 1024);
	kprintf("heap free: %x (%d MB)\n", alloc_free(), alloc_free() / 1024 / 1024);
	kprintf("free blocks: %d\n", alloc_free_blocks());
}

// malloc <size>
static void malloc_command(const char *arg){
	unsigned long size;
	if(arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
		size = k_hextoul(arg);
	else
		size = k_atoi(arg);
	void *ptr = kmalloc(size);
	if(ptr)
		kprintf("allocated %d bytes at %p\n", size, ptr);
	else
		kprintf("allocation failed (out of memory)\n");
}

// free <hex_addr>
static void free_command(const char *arg){
	unsigned long addr = k_hextoul(arg);
	kfree((void *)addr);
	kprintf("freed %x\n", addr);
}

// --- task commands ---

static void tasks_command(){
	static const char *state_names[] = {"unused", "ready", "running", "dead", "sleeping"};
	char buf[20];
	int n = task_count();

	// count alive tasks
	int alive = 0;
	extern task_t tasks[];
	for(int i = 0; i < n; i++){
		if(tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DEAD)
			alive++;
	}
	itoa(alive, buf, 20);
	uart_puts("tasks: ");
	uart_puts(buf);
	uart_puts(" active\n");

	for(int i = 0; i < n; i++){
		if(tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD)
			continue;
		uart_puts("  [");
		itoa(tasks[i].id, buf, 20);
		uart_puts(buf);
		uart_puts("] ");
		uart_puts(tasks[i].name);
		uart_puts("  ");
		uart_puts(state_names[tasks[i].state]);
		if(tasks[i].state == TASK_SLEEPING){
			uart_puts("  wake=");
			uart_puthex(tasks[i].wake_time);
		}
		uart_puts("\n");
	}
}

static void kill_command(const char *arg){
	int id = k_atoi(arg);
	if(id == 0){
		uart_puts("cannot kill kernel task\n");
		return;
	}
	if(task_kill(id) < 0){
		uart_puts("failed to kill task ");
	} else {
		uart_puts("killed task ");
	}
	char buf[12];
	itoa(id, buf, 12);
	uart_puts(buf);
	uart_puts("\n");
}

// a demo background task that counts and prints periodically
static void counter_task_fn(void){
	int count = 0;
	while(1){
		count++;
		if(count % 5 == 0){
			char buf[20];
			uart_puts("[counter] ");
			itoa(count, buf, 20);
			uart_puts(buf);
			uart_puts("\n");
		}
		task_sleep_ms(500);
	}
}

static void spawn_command(){
	int id = task_create_named(counter_task_fn, "counter");
	if(id < 0){
		kprintf("failed to create task\n");
		return;
	}
	kprintf("spawned task %d\n", id);
}

static void sleep_command(const char *arg){
	unsigned long ms = k_atoi(arg);
	char buf[20];
	itoa((int)ms, buf, 20);
	uart_puts("sleeping ");
	uart_puts(buf);
	uart_puts("ms...\n");
	task_sleep_ms(ms);
	uart_puts("woke up\n");
}

static void yield_command(){
	uart_puts("yielding...\n");
	yield();
	uart_puts("returned to shell\n");
}

// --- framebuffer test ---

static void fbtest_command(){
	if(fb_width() == 0){
		kprintf("no framebuffer\n");
		return;
	}
	// draw colored rectangles
	fb_clear(0xFF000020);
	fb_fill_rect(20, 20, 100, 80, 0xFFFF0000);  // red
	fb_fill_rect(140, 20, 100, 80, 0xFF00FF00);  // green
	fb_fill_rect(260, 20, 100, 80, 0xFF0000FF);  // blue
	fb_fill_rect(380, 20, 100, 80, 0xFFFFFF00);  // yellow

	// draw text
	fb_put_string(20, 120, "HudOS v1.0", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 140, "Bare metal Raspberry Pi 4 OS", 0xFF00FF00, 0xFF000020);
	fb_put_string(20, 170, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 180, "abcdefghijklmnopqrstuvwxyz", 0xFFFFFFFF, 0xFF000020);
	fb_put_string(20, 190, "0123456789 !@#$%^&*()-=+[]", 0xFFFFFFFF, 0xFF000020);

	// reset console below test area
	fb_console_init(0xFFFFFFFF, 0xFF000020);
	kprintf("fbtest: drew test pattern\n");
}

// --- spinlock test ---

static void locktest_command(){
	spinlock_t test = SPINLOCK_INIT;
	kprintf("spinlock test:\n");
	kprintf("  trylock: %d\n", spin_trylock(&test));   // should be 1 (acquired)
	kprintf("  trylock: %d\n", spin_trylock(&test));   // should be 0 (already held)
	spin_unlock(&test);
	kprintf("  unlocked, trylock: %d\n", spin_trylock(&test)); // should be 1 again
	spin_unlock(&test);
	kprintf("  lock/unlock pair... ");
	spin_lock(&test);
	spin_unlock(&test);
	kprintf("ok\n");
}

// --- semaphore producer/consumer demo ---

static semaphore_t demo_sem;
static volatile int demo_data;

static void producer_fn(void){
	for(int i = 1; i <= 5; i++){
		demo_data = i;
		kprintf("[producer] sent %d\n", i);
		sem_signal(&demo_sem);
		task_sleep_ms(200);
	}
}

static void consumer_fn(void){
	for(int i = 0; i < 5; i++){
		sem_wait(&demo_sem);
		kprintf("[consumer] got %d\n", demo_data);
	}
	kprintf("[consumer] done\n");
}

static void semtest_command(){
	sem_init(&demo_sem, 0);
	demo_data = 0;
	int c = task_create_named(consumer_fn, "consumer");
	int p = task_create_named(producer_fn, "producer");
	if(c < 0 || p < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned producer=%d consumer=%d\n", p, c);
}

// --- message queue ping-pong demo ---

#define MSG_PING 1
#define MSG_PONG 2

static msgqueue_t mq_a; // pinger sends here
static msgqueue_t mq_b; // ponger sends here

static void pinger_fn(void){
	for(int i = 1; i <= 5; i++){
		kprintf("[ping] sending %d\n", i);
		mq_send(&mq_a, MSG_PING, &i, sizeof(i));
		msg_t reply;
		mq_recv(&mq_b, &reply);
		int val;
		memcpy(&val, reply.data, sizeof(val));
		kprintf("[ping] got pong %d\n", val);
	}
	kprintf("[ping] done\n");
}

static void ponger_fn(void){
	for(int i = 0; i < 5; i++){
		msg_t msg;
		mq_recv(&mq_a, &msg);
		int val;
		memcpy(&val, msg.data, sizeof(val));
		kprintf("[pong] got %d, replying\n", val);
		val *= 10;
		mq_send(&mq_b, MSG_PONG, &val, sizeof(val));
	}
	kprintf("[pong] done\n");
}

static void mqtest_command(){
	mq_init(&mq_a);
	mq_init(&mq_b);
	int p = task_create_named(pinger_fn, "pinger");
	int q = task_create_named(ponger_fn, "ponger");
	if(p < 0 || q < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned pinger=%d ponger=%d\n", p, q);
}

// --- mutex shared counter demo ---

static mutex_t demo_mutex;
static volatile int shared_counter;

static void counter_a_fn(void){
	for(int i = 0; i < 10; i++){
		mutex_lock(&demo_mutex);
		shared_counter++;
		int val = shared_counter;
		mutex_unlock(&demo_mutex);
		kprintf("[A] counter=%d\n", val);
		yield();
	}
}

static void counter_b_fn(void){
	for(int i = 0; i < 10; i++){
		mutex_lock(&demo_mutex);
		shared_counter++;
		int val = shared_counter;
		mutex_unlock(&demo_mutex);
		kprintf("[B] counter=%d\n", val);
		yield();
	}
}

static void mutextest_command(){
	mutex_init(&demo_mutex);
	shared_counter = 0;
	int a = task_create_named(counter_a_fn, "counter-A");
	int b = task_create_named(counter_b_fn, "counter-B");
	if(a < 0 || b < 0){
		kprintf("failed to create tasks\n");
		return;
	}
	kprintf("spawned A=%d B=%d, yield to run\n", a, b);
}

// --- user mode demo ---

// this function runs at EL1 — can only use syscalls, no direct UART/MMIO
static void user_demo_fn(void){
	sys_puts("[user] hello from EL1!\n");
	for(int i = 0; i < 3; i++){
		sys_puts("[user] tick\n");
		sys_yield();
	}
	sys_puts("[user] exiting\n");
	sys_exit();
}

static void uspawn_command(){
	int id = user_task_create(user_demo_fn, "user-demo");
	if(id < 0){
		kprintf("failed to create user task\n");
		return;
	}
	kprintf("spawned user task %d\n", id);
}

// --- ramfs commands ---

static void mkfile_command(const char *arg){
	int fd = ramfs_create(arg);
	if(fd < 0){
		uart_puts("failed to create file\n");
		return;
	}
	char buf[12];
	itoa(fd, buf, 12);
	uart_puts("created file '");
	uart_puts(arg);
	uart_puts("' fd=");
	uart_puts(buf);
	uart_puts("\n");
}

// write <name> <data>
static void write_command(const char *arg){
	// find the space between name and data
	const char *name_start = arg;
	const char *data = next_arg(arg);
	// extract name
	char name[32];
	int i = 0;
	while(name_start < data - 1 && i < 31){
		if(*name_start == ' ') break;
		name[i++] = *name_start++;
	}
	name[i] = '\0';

	int fd = ramfs_find(name);
	if(fd < 0){
		fd = ramfs_create(name);
		if(fd < 0){
			uart_puts("failed to create file\n");
			return;
		}
	}
	int len = k_strlen(data);
	ramfs_write(fd, data, len);
	char buf[12];
	itoa(len, buf, 12);
	uart_puts("wrote ");
	uart_puts(buf);
	uart_puts(" bytes to '");
	uart_puts(name);
	uart_puts("'\n");
}

static void cat_command(const char *arg){
	int fd = ramfs_find(arg);
	if(fd < 0){
		uart_puts("file not found: ");
		uart_puts(arg);
		uart_puts("\n");
		return;
	}
	char buf[RAMFS_MAX_SIZE + 1];
	int len = ramfs_read(fd, buf, RAMFS_MAX_SIZE);
	buf[len] = '\0';
	uart_puts(buf);
	uart_puts("\n");
}

static void ls_command(){
	int count = ramfs_count();
	if(count == 0){
		uart_puts("(no files)\n");
		return;
	}
	char buf[20];
	for(int i = 0; i < RAMFS_MAX_FILES; i++){
		const char *name = ramfs_name(i);
		if(name[0] == '\0') continue;
		uart_puts("  ");
		uart_puts(name);
		uart_puts("  ");
		itoa(ramfs_size(i), buf, 20);
		uart_puts(buf);
		uart_puts(" bytes\n");
	}
}

// --- self-test commands ---

static void test_uart_command(void){
	int r = test_uart();
	if(r == 0) kprintf("test_uart: PASSED\n");
	else        kprintf("test_uart: %d FAILURE(S)\n", r);
}

static void test_gpio_command(void){
	int r = test_gpio();
	if(r == 0) kprintf("test_gpio: PASSED\n");
	else        kprintf("test_gpio: %d FAILURE(S)\n", r);
}

static void test_timer_command(void){
	int r = test_timer();
	if(r == 0) kprintf("test_timer: PASSED\n");
	else        kprintf("test_timer: %d FAILURE(S)\n", r);
}

static void test_alloc_command(void){
	int r = test_alloc();
	if(r == 0) kprintf("test_alloc: PASSED\n");
	else        kprintf("test_alloc: %d FAILURE(S)\n", r);
}

static void test_all_command(void){
	int total = 0;
	kprintf("Running all self-tests...\n");
	total += test_uart();
	total += test_gpio();
	total += test_timer();
	total += test_alloc();
	kprintf("==============================\n");
	if(total == 0) kprintf("ALL TESTS PASSED\n");
	else            kprintf("TOTAL FAILURES: %d\n", total);
	kprintf("==============================\n");
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
	} else if(str_starts_with(buffer, "crashtest ")){
		crashtest_command(buffer + 10);
	} else if(str_eq(buffer, "crashtest")){
		crashtest_command("");
	} else if(str_eq(buffer, "ticks")){
		ticks_command();
	} else if(str_eq(buffer, "irqtest")){
		irqtest_command();
	} else if(str_eq(buffer, "timerdbg")){
		irq_dump_gic();
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
	} else if(str_starts_with(buffer, "free ")){
		free_command(buffer + 5);
	} else if(str_eq(buffer, "tasks")){
		tasks_command();
	} else if(str_eq(buffer, "spawn")){
		spawn_command();
	} else if(str_starts_with(buffer, "kill ")){
		kill_command(buffer + 5);
	} else if(str_starts_with(buffer, "sleep ")){
		sleep_command(buffer + 6);
	} else if(str_eq(buffer, "yield")){
		yield_command();
	} else if(str_eq(buffer, "uspawn")){
		uspawn_command();
	} else if(str_eq(buffer, "fbtest")){
		fbtest_command();
	} else if(str_eq(buffer, "fbmirror")){
		int m = !fb_get_mirror();
		fb_set_mirror(m);
		kprintf("fb mirror: %s\n", m ? "on" : "off");
	} else if(str_eq(buffer, "home stop")){
		home_stop();
		kprintf("desktop stopped\n");
	} else if(str_eq(buffer, "home")){
		if(fb_width() == 0){ kprintf("no framebuffer\n"); }
		else if(home_active()){ kprintf("desktop already running\n"); }
		else { home_start(); kprintf("desktop started\n"); }
	} else if(str_eq(buffer, "locktest")){
		locktest_command();
	} else if(str_eq(buffer, "semtest")){
		semtest_command();
	} else if(str_eq(buffer, "mqtest")){
		mqtest_command();
	} else if(str_eq(buffer, "mutextest")){
		mutextest_command();
	} else if(str_starts_with(buffer, "mkfile ")){
		mkfile_command(buffer + 7);
	} else if(str_starts_with(buffer, "write ")){
		write_command(buffer + 6);
	} else if(str_starts_with(buffer, "cat ")){
		cat_command(buffer + 4);
	} else if(str_eq(buffer, "ls")){
		ls_command();
	} else if(str_eq(buffer, "test_uart")){
		test_uart_command();
	} else if(str_eq(buffer, "test_gpio")){
		test_gpio_command();
	} else if(str_eq(buffer, "test_timer")){
		test_timer_command();
	} else if(str_eq(buffer, "test_alloc")){
		test_alloc_command();
	} else if(str_eq(buffer, "test_all")){
		test_all_command();
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

	kprintf("\n");
	kprintf("============================================\n");
	kprintf("  %s v%s\n", HUDOS_NAME, HUDOS_VERSION);
	kprintf("  Bare metal %s OS\n", HUDOS_ARCH);
	kprintf("  Board: %s\n", HUDOS_BOARD);
	kprintf("  Running at %s\n", HUDOS_EL);
	kprintf("  Built: %s %s\n", __DATE__, __TIME__);
	kprintf("  Author: %s\n", HUDOS_AUTHOR);
	kprintf("============================================\n");
	kprintf("\n");

	// initialize heap (needed before MMU for page table alloc)
	alloc_init();
	kprintf("heap initialized\n");

	// set up MMU
	mmu_init();
	kprintf("MMU enabled\n");

	// set up framebuffer
	if(fb_init(640, 480) == 0){
		kprintf("framebuffer: 640x480x32 pitch=%d\n", fb_pitch());
		fb_console_init(0xFFFFFFFF, 0xFF000020); // white on dark blue
		fb_console_puts("HudOS framebuffer initialized\n");
	} else {
		kprintf("framebuffer: init failed (serial only)\n");
	}

	// set up interrupts
	irq_init();
	timer_init(10);              // timer IRQ every 10ms (100Hz tick)
	uart_irq_enable();
	irq_enable(IRQ_UART);
	cpu_enable_irqs();

	kprintf("IRQs enabled (timer 10ms, UART RX)\n");
	kprintf("timer freq: %x Hz\n", timer_get_freq());

	// initialize task system
	task_init();
	kprintf("scheduler initialized\n");

	// initialize ram filesystem
	ramfs_init();
	kprintf("ramfs initialized\n");

	query_terminal(terminal_Buffer, terminal_Len);

	while (1) {
	}
}
