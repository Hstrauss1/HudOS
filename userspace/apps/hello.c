#include "../ulib.h"

int main(void){
    u_puts("hello from userspace C\n");
    u_puts("sleeping for 250ms...\n");
    u_sleep(250);
    u_puts("done\n");
    return 0;
}
