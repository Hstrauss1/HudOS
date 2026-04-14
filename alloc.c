//Hudson Strauss
//
// Free-list allocator with block headers.
// Each allocation has a header storing size. Freed blocks go on a linked list.
// kmalloc scans the free list first (first-fit), falls back to bump allocation.
//
#include "alloc.h"
#include "spinlock.h"
#include "panic.h"

// linker symbols
extern unsigned char __heap_start;
extern unsigned char __heap_end;

// block header: sits just before the returned pointer
// size includes the header itself
typedef struct free_block {
	unsigned long size;
	struct free_block *next;
} free_block_t;

#define HEADER_SIZE  sizeof(free_block_t)  // 16 bytes on AArch64
#define MIN_BLOCK    (HEADER_SIZE + 16)    // smallest useful block

static unsigned char *heap_ptr;  // bump pointer for virgin heap
static unsigned char *heap_end;
static free_block_t *free_list;  // head of free list
static unsigned long total_allocated;
static unsigned long free_block_count;
static spinlock_t heap_lock = SPINLOCK_INIT;

void alloc_init(void){
	heap_ptr = &__heap_start;
	heap_end = &__heap_end;
	free_list = (free_block_t *)0;
	total_allocated = 0;
	free_block_count = 0;
}

// align value up to alignment (must be power of 2)
static unsigned long align_up(unsigned long val, unsigned long align){
	return (val + align - 1) & ~(align - 1);
}

void *kmalloc(unsigned long size){
	return kmalloc_aligned(size, 16);
}

void *kmalloc_aligned(unsigned long size, unsigned long align){
	ASSERT(size > 0);
	ASSERT(align && (align & (align - 1)) == 0); // must be power of 2
	if(align < 16) align = 16;

	// total bytes needed: header + payload, aligned
	unsigned long total = align_up(HEADER_SIZE + size, align);
	if(total < MIN_BLOCK) total = MIN_BLOCK;

	unsigned long flags = spin_lock_irqsave(&heap_lock);

	// 1) scan free list for first fit
	free_block_t **prev = &free_list;
	free_block_t *blk = free_list;
	while(blk){
		unsigned long user_addr = (unsigned long)blk + HEADER_SIZE;
		if(blk->size >= total && (align == 16 || (user_addr & (align - 1)) == 0)){
			// found a fit — remove from free list
			*prev = blk->next;
			free_block_count--;
			blk->next = (free_block_t *)0;
			total_allocated += blk->size;
			spin_unlock_irqrestore(&heap_lock, flags);
			// return pointer past header
			return (void *)((unsigned char *)blk + HEADER_SIZE);
		}
		prev = &blk->next;
		blk = blk->next;
	}

	// 2) bump allocate from virgin heap
	unsigned long addr = align_up((unsigned long)heap_ptr, align);
	unsigned char *block_start = (unsigned char *)(addr - HEADER_SIZE);
	// make sure header is within heap
	if(block_start < heap_ptr)
		block_start = heap_ptr;
	addr = (unsigned long)(block_start + HEADER_SIZE);
	// re-align the user pointer
	addr = align_up(addr, align);
	block_start = (unsigned char *)(addr - HEADER_SIZE);

	if((unsigned char *)addr + size > heap_end){
		spin_unlock_irqrestore(&heap_lock, flags);
		return (void *)0; // out of memory
	}

	free_block_t *hdr = (free_block_t *)block_start;
	hdr->size = total;
	hdr->next = (free_block_t *)0;

	heap_ptr = block_start + total;
	total_allocated += total;
	spin_unlock_irqrestore(&heap_lock, flags);
	return (void *)addr;
}

void kfree(void *ptr){
	if(!ptr) return;

	// header is right before the pointer
	free_block_t *blk = (free_block_t *)((unsigned char *)ptr - HEADER_SIZE);

	unsigned long flags = spin_lock_irqsave(&heap_lock);
	total_allocated -= blk->size;

	// insert at head of free list
	blk->next = free_list;
	free_list = blk;
	free_block_count++;
	spin_unlock_irqrestore(&heap_lock, flags);
}

unsigned long alloc_used(void){
	return total_allocated;
}

unsigned long alloc_free(void){
	// virgin heap remaining + free list blocks
	unsigned long virgin = (unsigned long)(heap_end - heap_ptr);
	free_block_t *blk = free_list;
	while(blk){
		virgin += blk->size;
		blk = blk->next;
	}
	return virgin;
}

unsigned long alloc_free_blocks(void){
	return free_block_count;
}
