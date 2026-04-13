//Hudson Strauss
#include "syscall.h"
#include "uart.h"
#include "timer.h"
#include "sched.h"
#include "task.h"
#include "vfs.h"

unsigned long syscall_handler(unsigned long num, unsigned long arg0, unsigned long arg1, unsigned long arg2){
	(void)arg1;

	switch(num){
		case SYS_PUTC:
			uart_putc((char)arg0);
			return 0;
		case SYS_PUTS:
			uart_puts((const char *)arg0);
			return 0;
		case SYS_TICKS:
			return timer_get_irq_count();
		case SYS_YIELD:
			yield();
			return 0;
		case SYS_EXIT:
			task_exit();
			return 0; // unreachable
		case SYS_SLEEP:
			task_sleep_ms(arg0);
			return 0;
		case SYS_OPEN:
			return (unsigned long)vfs_open((const char *)arg0, (int)arg1);
		case SYS_CLOSE:
			vfs_close((int)arg0);
			return 0;
		case SYS_READ:
			return (unsigned long)vfs_read((int)arg0, (void *)arg1, (int)arg2);
		case SYS_WRITE:
			return (unsigned long)vfs_write((int)arg0, (const void *)arg1, (int)arg2);
		case SYS_SEEK:
			return (unsigned long)vfs_seek((int)arg0, (int)arg1, (int)arg2);
		default:
			uart_puts("[syscall] unknown: ");
			uart_puthex(num);
			uart_puts("\n");
			return (unsigned long)-1;
	}
}
