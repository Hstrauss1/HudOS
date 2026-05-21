long u_puts(char *s);
long u_putc(char c);
long u_sleep(long ms);

int main(void){
    int i;
    int total;
    total = 0;

    u_puts("Tiny C quick demo\n");

    for(i = 0; i < 4; i = i + 1){
        total = total + i;
        u_putc('0' + i);
    }

    u_puts("\nsum=");
    u_putc('0' + total);
    u_puts("\n");

    u_sleep(100);
    return 0;
}
