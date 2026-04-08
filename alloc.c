//Hudson Strauss
#include "alloc.h"

// linker symbols
extern unsigned char __heap_start;
extern unsigned char __heap_end;

static unsigned char *heap_ptr;
static unsigned char *heap_end;

void alloc_init(void){
	heap_ptr = &__heap_start;
	heap_end = &__heap_end;
}

void *kmalloc(unsigned long size){
	return kmalloc_aligned(size, 16);
}

void *kmalloc_aligned(unsigned long size, unsigned long align){
	// align the pointer up
	unsigned long addr = (unsigned long)heap_ptr;
	unsigned long mask = align - 1;
	addr = (addr + mask) & ~mask;

	unsigned char *result = (unsigned char *)addr;
	if(result + size > heap_end)
		return (void *)0; // out of memory

	heap_ptr = result + size;
	return result;
}

unsigned long alloc_used(void){
	return (unsigned long)(heap_ptr - &__heap_start);
}

unsigned long alloc_free(void){
	return (unsigned long)(heap_end - heap_ptr);
}
