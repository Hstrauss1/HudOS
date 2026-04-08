//Hudson Strauss
#include "string.h"

// ----- string helpers -----

int k_strlen(const char *s){
	int len = 0;
	while(*s++){
		++len;
	}
	return len;
}

int str_eq(const char *a, const char *b){
	while(*a && *b){
		if(*a != *b) return 0;
		++a;
		++b;
	}
	return *a == *b;
}

int k_strncmp(const char *a, const char *b, int n){
	while(n-- > 0){
		if(*a != *b) return *a - *b;
		if(*a == '\0') return 0;
		++a;
		++b;
	}
	return 0;
}

int str_starts_with(const char *s, const char *prefix){
	while(*prefix){
		if(*s != *prefix) return 0;
		++s;
		++prefix;
	}
	return 1;
}

// ----- memory helpers -----

void *memset(void *dst, int val, unsigned long n){
	unsigned char *p = (unsigned char *)dst;
	while(n--){
		*p++ = (unsigned char)val;
	}
	return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n){
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;
	while(n--){
		*d++ = *s++;
	}
	return dst;
}

// ----- number printing -----

int itoa(int value, char *buf, int bufLen){
	int i = 0;
	int neg = 0;

	if(bufLen <= 0) return 0;

	if(value < 0){
		neg = 1;
		value = -value;
	}

	// write digits in reverse
	do {
		if(i >= bufLen - 1) break;
		buf[i++] = '0' + (value % 10);
		value /= 10;
	} while(value > 0);

	if(neg && i < bufLen - 1){
		buf[i++] = '-';
	}
	buf[i] = '\0';

	// reverse the string
	int j = 0;
	int k = i - 1;
	while(j < k){
		char tmp = buf[j];
		buf[j] = buf[k];
		buf[k] = tmp;
		++j;
		--k;
	}

	return i;
}

int k_atoi(const char *s){
	int result = 0;
	int neg = 0;
	if(*s == '-'){
		neg = 1;
		++s;
	}
	while(*s >= '0' && *s <= '9'){
		result = result * 10 + (*s - '0');
		++s;
	}
	return neg ? -result : result;
}

// parse hex string, with or without "0x" prefix
unsigned long k_hextoul(const char *s){
	unsigned long result = 0;
	if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	while(1){
		char c = *s++;
		if(c >= '0' && c <= '9')
			result = (result << 4) | (c - '0');
		else if(c >= 'a' && c <= 'f')
			result = (result << 4) | (c - 'a' + 10);
		else if(c >= 'A' && c <= 'F')
			result = (result << 4) | (c - 'A' + 10);
		else
			break;
	}
	return result;
}
