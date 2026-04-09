//Hudson Strauss
#ifndef SPINLOCK_H
#define SPINLOCK_H

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

// initialize a spinlock to unlocked state
static inline void spin_init(spinlock_t *s){
	s->lock = 0;
}

// acquire: spins until lock is obtained (uses AArch64 exclusives)
static inline void spin_lock(spinlock_t *s){
	unsigned int tmp, val;
	__asm__ volatile(
		"1: ldaxr %w0, [%2]\n"      // load-acquire exclusive
		"   cbnz  %w0, 1b\n"        // if locked, retry
		"   mov   %w1, #1\n"
		"   stxr  %w0, %w1, [%2]\n" // try to store 1
		"   cbnz  %w0, 1b\n"        // if store failed, retry
		: "=&r"(tmp), "=&r"(val)
		: "r"(&s->lock)
		: "memory"
	);
}

// release: store 0 with release semantics
static inline void spin_unlock(spinlock_t *s){
	__asm__ volatile(
		"stlr wzr, [%0]\n"
		:: "r"(&s->lock)
		: "memory"
	);
}

// try to acquire without spinning, returns 1 if acquired, 0 if not
static inline int spin_trylock(spinlock_t *s){
	unsigned int tmp, val;
	__asm__ volatile(
		"ldaxr %w0, [%2]\n"
		"cbnz  %w0, 1f\n"
		"mov   %w1, #1\n"
		"stxr  %w0, %w1, [%2]\n"
		"cbz   %w0, 2f\n"
		"1: mov %w0, #1\n"    // failed
		"b 3f\n"
		"2: mov %w0, #0\n"    // success
		"3:\n"
		: "=&r"(tmp), "=&r"(val)
		: "r"(&s->lock)
		: "memory"
	);
	return tmp == 0;
}

// acquire lock with IRQs disabled (returns previous DAIF for restore)
static inline unsigned long spin_lock_irqsave(spinlock_t *s){
	unsigned long daif;
	__asm__ volatile("mrs %0, DAIF" : "=r"(daif));
	__asm__ volatile("msr DAIFSet, #2"); // mask IRQs
	spin_lock(s);
	return daif;
}

// release lock and restore IRQ state
static inline void spin_unlock_irqrestore(spinlock_t *s, unsigned long daif){
	spin_unlock(s);
	__asm__ volatile("msr DAIF, %0" :: "r"(daif));
}

#endif
