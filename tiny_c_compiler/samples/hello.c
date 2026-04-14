long u_puts(char *s);
long u_sleep(long ms);

int main(void){
    u_puts("hello from tiny_c_compiler\n");
    u_puts("sleeping for 250ms...\n");
    u_sleep(250);
    u_puts("done\n");
    return 0;
}
