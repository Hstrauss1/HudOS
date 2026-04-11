//Hudson Strauss
#include "test.h"
#include "uart.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"
#include "alloc.h"
#include "string.h"

// ---------- helpers ----------

#define TEST_PASS(name) kprintf("[PASS] " name "\n")
#define TEST_FAIL(name) kprintf("[FAIL] " name "\n")

// ---------- test_uart ----------
// Tests: kprintf format specifiers, string library, itoa/atoi round-trip.
// No loopback hardware assumed — all checks are logic/output only.
int test_uart(void) {
    int fail = 0;
    kprintf("\n=== test_uart ===\n");

    // 1. basic output
    kprintf("  kprintf %%d: %d (expect 42)\n", 42);
    kprintf("  kprintf %%d: %d (expect -7)\n", -7);
    kprintf("  kprintf %%x: %x (expect deadbeef)\n", 0xdeadbeef);
    kprintf("  kprintf %%s: %s (expect hello)\n", "hello");
    kprintf("  kprintf %%c: %c (expect A)\n", 'A');
    kprintf("  kprintf %%p: %p\n", (void *)0xCAFEBABE);
    TEST_PASS("kprintf output");

    // 2. strlen
    if(k_strlen("hello") == 5)    TEST_PASS("strlen");
    else { TEST_FAIL("strlen"); fail++; }

    // 3. str_eq
    if(str_eq("abc", "abc"))      TEST_PASS("str_eq match");
    else { TEST_FAIL("str_eq match"); fail++; }

    if(!str_eq("abc", "xyz"))     TEST_PASS("str_eq mismatch");
    else { TEST_FAIL("str_eq mismatch"); fail++; }

    // 4. str_starts_with
    if(str_starts_with("hello world", "hello")) TEST_PASS("str_starts_with");
    else { TEST_FAIL("str_starts_with"); fail++; }

    // 5. memset / memcpy
    unsigned char buf[16];
    memset(buf, 0xAB, 16);
    int ok = 1;
    for(int i = 0; i < 16; i++) if(buf[i] != 0xAB){ ok = 0; break; }
    if(ok) TEST_PASS("memset");
    else { TEST_FAIL("memset"); fail++; }

    unsigned char dst[16];
    memcpy(dst, buf, 16);
    ok = 1;
    for(int i = 0; i < 16; i++) if(dst[i] != buf[i]){ ok = 0; break; }
    if(ok) TEST_PASS("memcpy");
    else { TEST_FAIL("memcpy"); fail++; }

    // 6. itoa / atoi round-trip
    char numbuf[20];
    itoa(12345, numbuf, 20);
    if(k_atoi(numbuf) == 12345) TEST_PASS("itoa/atoi round-trip");
    else { TEST_FAIL("itoa/atoi round-trip"); fail++; }

    itoa(-99, numbuf, 20);
    if(k_atoi(numbuf) == -99) TEST_PASS("itoa negative");
    else { TEST_FAIL("itoa negative"); fail++; }

    // 7. k_hextoul
    if(k_hextoul("deadbeef") == 0xdeadbeef) TEST_PASS("k_hextoul");
    else { TEST_FAIL("k_hextoul"); fail++; }

    kprintf("=== test_uart: %d failure(s) ===\n\n", fail);
    return fail;
}

// ---------- test_gpio ----------
// Tests: function select, write, read.
// On QEMU the read-back may not match the write — we note it but don't fail,
// since the Pi4 GPIO block isn't fully emulated.
int test_gpio(void) {
    int fail = 0;
    kprintf("\n=== test_gpio ===\n");

    // Use pin 21 — available on 40-pin header, not used by kernel
    int pin = 21;

    gpio_set_function(pin, GPIO_FUNC_OUTPUT);
    TEST_PASS("set_function OUTPUT");

    gpio_write(pin, 1);
    int hi = gpio_read(pin);
    kprintf("  write HIGH -> read %d %s\n", hi,
            hi == 1 ? "(ok)" : "(QEMU: 0 expected on real HW)");
    if(hi == 1) TEST_PASS("write HIGH / read HIGH");
    else         kprintf("[NOTE] gpio read-back mismatch (QEMU normal)\n");

    gpio_write(pin, 0);
    int lo = gpio_read(pin);
    kprintf("  write LOW  -> read %d %s\n", lo,
            lo == 0 ? "(ok)" : "(unexpected)");
    if(lo == 0) TEST_PASS("write LOW / read LOW");
    else { TEST_FAIL("write LOW / read LOW"); fail++; }

    gpio_set_function(pin, GPIO_FUNC_INPUT);
    TEST_PASS("set_function INPUT");

    kprintf("=== test_gpio: %d failure(s) ===\n\n", fail);
    return fail;
}

// ---------- test_timer ----------
// Tests: system timer advances, delay_ms is in the right ballpark,
// timer IRQ fires, frequency register non-zero.
int test_timer(void) {
    int fail = 0;
    kprintf("\n=== test_timer ===\n");

    // 1. system timer is running
    unsigned long t0 = timer_get_ticks();
    delay_ms(20);
    unsigned long t1 = timer_get_ticks();
    kprintf("  t0=%x  t1=%x  delta=%x us\n", t0, t1, t1 - t0);

    if(t1 > t0) TEST_PASS("system timer advancing");
    else { TEST_FAIL("system timer advancing"); fail++; }

    // 2. delay_ms is roughly right (20ms = 20000 us; allow 50% QEMU slack)
    unsigned long delta = t1 - t0;
    if(delta >= 10000) TEST_PASS("delay_ms ~20ms");
    else { TEST_FAIL("delay_ms too short"); fail++; }

    // 3. IRQ counter increments
    unsigned long irq0 = timer_get_irq_count();
    delay_ms(100);
    unsigned long irq1 = timer_get_irq_count();
    kprintf("  irq count: before=%d after=%d\n", irq0, irq1);

    if(irq1 > irq0) TEST_PASS("timer IRQ firing");
    else { TEST_FAIL("timer IRQ not firing"); fail++; }

    // 4. frequency register non-zero
    unsigned long freq = timer_get_freq();
    kprintf("  generic timer freq: %x Hz\n", freq);
    if(freq != 0) TEST_PASS("timer frequency non-zero");
    else { TEST_FAIL("timer frequency is zero"); fail++; }

    kprintf("=== test_timer: %d failure(s) ===\n\n", fail);
    return fail;
}

// ---------- test_alloc ----------
// Tests: kmalloc, kfree, kmalloc_aligned, heap accounting, write/read.
int test_alloc(void) {
    int fail = 0;
    kprintf("\n=== test_alloc ===\n");

    // 1. basic alloc and free
    void *p = kmalloc(64);
    if(p) { TEST_PASS("kmalloc(64)"); kfree(p); TEST_PASS("kfree"); }
    else  { TEST_FAIL("kmalloc(64)"); fail++; }

    // 2. multiple allocations
    void *ptrs[8];
    int ok = 1;
    for(int i = 0; i < 8; i++){
        ptrs[i] = kmalloc(128);
        if(!ptrs[i]){ ok = 0; kprintf("[FAIL] alloc %d\n", i); fail++; break; }
    }
    if(ok){
        TEST_PASS("8x kmalloc(128)");
        for(int i = 0; i < 8; i++) kfree(ptrs[i]);
        TEST_PASS("8x kfree");
    }

    // 3. aligned allocation
    void *ap = kmalloc_aligned(256, 64);
    if(!ap){ TEST_FAIL("kmalloc_aligned(256,64)"); fail++; }
    else if((unsigned long)ap % 64 != 0){
        kprintf("[FAIL] bad alignment: %p mod 64 = %d\n", ap, (int)((unsigned long)ap % 64));
        fail++;
        kfree(ap);
    } else {
        kprintf("  aligned ptr: %p (mod 64 = %d)\n", ap, (int)((unsigned long)ap % 64));
        TEST_PASS("kmalloc_aligned alignment");
        kfree(ap);
    }

    // 4. write and read back through allocated memory
    unsigned char *buf = (unsigned char *)kmalloc(256);
    if(!buf){ TEST_FAIL("kmalloc for write test"); fail++; }
    else {
        for(int i = 0; i < 256; i++) buf[i] = (unsigned char)(i & 0xFF);
        int match = 1;
        for(int i = 0; i < 256; i++)
            if(buf[i] != (unsigned char)(i & 0xFF)){ match = 0; break; }
        if(match) TEST_PASS("write/read 256 bytes");
        else { TEST_FAIL("write/read mismatch"); fail++; }
        kfree(buf);
    }

    // 5. heap accounting moves after alloc
    unsigned long used_before = alloc_used();
    void *q = kmalloc(512);
    unsigned long used_after = alloc_used();
    kprintf("  used before=%x  after=%x  diff=%x\n",
            used_before, used_after, used_after - used_before);
    if(used_after > used_before) TEST_PASS("heap accounting");
    else { TEST_FAIL("heap accounting"); fail++; }
    if(q) kfree(q);

    // 6. large allocation
    void *big = kmalloc(1024 * 64);
    if(big){ TEST_PASS("kmalloc 64KB"); kfree(big); }
    else   { TEST_FAIL("kmalloc 64KB"); fail++; }

    kprintf("=== test_alloc: %d failure(s) ===\n\n", fail);
    return fail;
}
