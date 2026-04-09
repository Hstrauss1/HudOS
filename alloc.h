//Hudson Strauss
#ifndef ALLOC_H
#define ALLOC_H

void alloc_init(void);
void *kmalloc(unsigned long size);
void *kmalloc_aligned(unsigned long size, unsigned long align);
void kfree(void *ptr);

// info for debugging
unsigned long alloc_used(void);
unsigned long alloc_free(void);
unsigned long alloc_free_blocks(void);

#endif
