long u_puts(char *s);
long u_putc(char c);

char gch;
int gnums[3];

int add2(int a, int b){
    return a + b;
}

int read_ptr(int *p){
    return *(p + 2);
}

int main(void){
    int nums[3];
    int *p;

    gch = 'A';
    gnums[0] = 1;
    gnums[1] = 2;
    gnums[2] = 3;

    nums[0] = 4;
    nums[1] = 5;
    nums[2] = 6;
    p = &nums[0];

    u_putc(gch);
    u_putc('0' + gnums[1]);
    u_putc('0' + read_ptr(p));
    u_putc('0' + add2(3, 4));
    u_putc('0' + sizeof(char));
    u_putc('0' + sizeof(long));
    u_putc('0' + (int)(long)gnums[2]);
    u_puts("\n");
    return 0;
}
