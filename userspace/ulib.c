#include "ulib.h"

#ifdef USER_KERNEL_MODE

static volatile unsigned int *const uart_dr   = (volatile unsigned int *)0x09000000UL;
static volatile unsigned int *const uart_fr   = (volatile unsigned int *)0x09000018UL;

static unsigned long ticks(void){
    unsigned long v;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(v));
    return v;
}

static unsigned long freq(void){
    unsigned long v;
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(v));
    return v;
}

void u_putc(char c){
    while(*uart_fr & (1u << 5)){}
    *uart_dr = (unsigned int)c;
}

void u_puts(const char *s){
    while(*s){
        if(*s == '\n')
            u_putc('\r');
        u_putc(*s++);
    }
}

void u_exit(void){
    while(1){}
}

void u_sleep(unsigned long ms){
    unsigned long start = ticks();
    unsigned long delta = (freq() * ms) / 1000UL;
    while(ticks() - start < delta){}
}

void u_yield(void){
    __asm__ volatile("yield");
}

long u_open(const char *path, int flags){
    (void)path;
    (void)flags;
    return -1;
}

long u_read(int fd, void *buf, unsigned long len){
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
}

long u_write(int fd, const void *buf, unsigned long len){
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
}

void u_close(int fd){
    (void)fd;
}

#else

void u_putc(char c){
    sys_putc(c);
}

void u_puts(const char *s){
    sys_puts(s);
}

void u_exit(void){
    sys_exit();
    while(1){}
}

void u_sleep(unsigned long ms){
    sys_sleep(ms);
}

void u_yield(void){
    sys_yield();
}

long u_open(const char *path, int flags){
    return sys_open(path, flags);
}

long u_read(int fd, void *buf, unsigned long len){
    return sys_read(fd, buf, len);
}

long u_write(int fd, const void *buf, unsigned long len){
    return sys_write(fd, buf, len);
}

void u_close(int fd){
    sys_close(fd);
}

#endif
