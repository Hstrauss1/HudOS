//Hudson Strauss
#ifndef PANIC_H
#define PANIC_H

// halt the system with a message — never returns
void panic(const char *msg) __attribute__((noreturn));

// halt with file/line info — never returns
void panic_at(const char *msg, const char *file, int line) __attribute__((noreturn));

// assert macro — on failure, prints expression, file, line and halts
#define ASSERT(expr) \
	do { if(!(expr)) panic_at("ASSERT failed: " #expr, __FILE__, __LINE__); } while(0)

// kernel bug — same as panic but semantically different
#define BUG(msg) panic_at(msg, __FILE__, __LINE__)

#endif
