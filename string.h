//Hudson Strauss
#ifndef STRING_H
#define STRING_H

int k_strlen(const char *s);
int str_eq(const char *a, const char *b);
int k_strncmp(const char *a, const char *b, int n);
int str_starts_with(const char *s, const char *prefix);

void *memset(void *dst, int val, unsigned long n);
void *memcpy(void *dst, const void *src, unsigned long n);

int itoa(int value, char *buf, int bufLen);
int k_atoi(const char *s);
unsigned long k_hextoul(const char *s);

#endif
