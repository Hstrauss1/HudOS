//Hudson Strauss
#ifndef USER_H
#define USER_H

// drop to EL1 and execute fn with the given stack pointer
void user_enter_el1(void (*fn)(void), unsigned long user_sp);

// create a task that drops to EL1 and runs fn using syscalls
// returns task ID or -1 on failure
int user_task_create(void (*fn)(void), const char *name);

#endif
