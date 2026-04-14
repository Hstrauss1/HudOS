long u_puts(char *s);
long u_putc(char c);
long u_sleep(long ms);

char banner;
int values[4];

int sum_pair(int a, int b){
    return a + b;
}

int main(void){
    int i;
    int total;
    int *p;

    banner = 'H';
    values[0] = 1;
    values[1] = 2;
    values[2] = 3;
    values[3] = 4;

    p = &values[0];
    total = 0;

    u_puts("Tiny C quick demo: ");
    u_putc(banner);
    u_puts("\n");

    for(i = 0; i < 4; i = i + 1){
        total = total + p[i];
        u_putc('0' + p[i]);
    }

    u_puts("\nsum=");
    u_putc('0' + total);
    u_puts("\npair=");
    u_putc('0' + sum_pair(values[1], values[2]));
    u_puts("\nsize(long)=");
    u_putc('0' + sizeof(long));
    u_puts("\n");

    u_sleep(100);
    return 0;
}
