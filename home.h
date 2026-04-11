//Hudson Strauss
#ifndef HOME_H
#define HOME_H

// spawn the live desktop task (no-op if already running)
void home_start(void);

// signal the desktop task to exit
void home_stop(void);

// returns 1 if the desktop task is running
int home_active(void);

#endif
