#ifndef USERSPACE_ULIB_H
#define USERSPACE_ULIB_H

#include "../syscall.h"
#include "../vfs.h"

void u_putc(char c);
void u_puts(const char *s);
void u_exit(void);
void u_sleep(unsigned long ms);
void u_yield(void);
long u_open(const char *path, int flags);
long u_read(int fd, void *buf, unsigned long len);
long u_write(int fd, const void *buf, unsigned long len);
void u_close(int fd);

#endif
