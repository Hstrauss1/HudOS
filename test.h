//Hudson Strauss
#ifndef TEST_H
#define TEST_H

// Self-test commands — return 0 on all pass, >0 = number of failures
int test_uart(void);
int test_gpio(void);
int test_timer(void);
int test_alloc(void);

#endif
