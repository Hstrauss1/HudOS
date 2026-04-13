//Hudson Strauss
#include "sd.h"
#include "timer.h"
#include "uart.h"

// ── BCM2711 Arasan SDHCI registers (EMMC, base 0xFE300000) ───────────────────
// Reference: BCM2711 peripherals datasheet, section on EMMC

#define EMMC_BASE       0xFE300000UL
#define REG(off)        (*(volatile unsigned int *)(EMMC_BASE + (off)))

#define EMMC_ARG2       REG(0x00)   // argument 2 (ACMD23 pre-erase count)
#define EMMC_BLKSIZECNT REG(0x04)   // [31:16]=block count, [15:0]=block size
#define EMMC_ARG1       REG(0x08)   // command argument
#define EMMC_CMDTM      REG(0x0C)   // command + transfer mode
#define EMMC_RESP0      REG(0x10)   // response bits [31:0]
#define EMMC_RESP1      REG(0x14)   // response bits [63:32]
#define EMMC_RESP2      REG(0x18)   // response bits [95:64]
#define EMMC_RESP3      REG(0x1C)   // response bits [127:96]
#define EMMC_DATA       REG(0x20)   // data read/write port
#define EMMC_STATUS     REG(0x24)   // controller/card status
#define EMMC_CONTROL0   REG(0x28)   // host control 0
#define EMMC_CONTROL1   REG(0x2C)   // host control 1: clock, resets
#define EMMC_INTERRUPT  REG(0x30)   // interrupt flags (write-1-to-clear)
#define EMMC_IRPT_MASK  REG(0x34)   // which interrupts show in EMMC_INTERRUPT
#define EMMC_IRPT_EN    REG(0x38)   // which interrupts generate a CPU IRQ
#define EMMC_CONTROL2   REG(0x3C)   // host control 2

// ── CMDTM bit fields ──────────────────────────────────────────────────────────

#define CMD(n)          ((n) << 24)
#define RSPNS_NONE      (0 << 16)
#define RSPNS_136       (1 << 16)   // 136-bit CID/CSD
#define RSPNS_48        (2 << 16)   // normal 48-bit
#define RSPNS_48B       (3 << 16)   // 48-bit + busy (CMD7, CMD12)
#define CMD_IXCHK       (1 << 20)   // verify command index in response
#define CMD_CRCCHK      (1 << 19)   // verify CRC in response
#define CMD_ISDATA      (1 << 21)   // command transfers data
#define TM_DAT_READ     (1 <<  4)   // 1 = card→host (read)
#define TM_BLKCNT_EN    (1 <<  1)
#define TM_MULTI        (1 <<  0)   // multi-block transfer
#define TM_AUTOCMD12    (1 <<  2)   // auto issue CMD12 after multi-block

// shorthand for normal R1 response
#define R1 (RSPNS_48 | CMD_IXCHK | CMD_CRCCHK)

// ── STATUS bits ───────────────────────────────────────────────────────────────

#define SR_CMD_INHIBIT  (1 << 0)
#define SR_DAT_INHIBIT  (1 << 1)

// ── INTERRUPT bits ────────────────────────────────────────────────────────────

#define INT_CMD_DONE    (1 <<  0)
#define INT_DATA_DONE   (1 <<  1)
#define INT_WRITE_RDY   (1 <<  4)
#define INT_READ_RDY    (1 <<  5)
#define INT_ERROR       (1 << 15)

// ── CONTROL1 bits ─────────────────────────────────────────────────────────────

#define C1_CLK_INTLEN   (1 <<  0)   // enable internal clock
#define C1_CLK_STABLE   (1 <<  1)   // internal clock stable (RO)
#define C1_CLK_EN       (1 <<  2)   // clock to card enabled
#define C1_TOUNIT_MAX   (0xEu << 16) // maximum data timeout
#define C1_SRST_HC      (1 << 24)   // full reset

// ── state ─────────────────────────────────────────────────────────────────────

static int sd_type = 0; // 0=not ready, 1=SDv1, 2=SDv2-SC, 3=SDv2-HC/XC
static unsigned int sd_rca = 0;

// ── helpers ───────────────────────────────────────────────────────────────────

// wait up to timeout_ms for interrupt bits 'mask'; returns 1 on success
static int wait_int(unsigned int mask, unsigned int timeout_ms){
    unsigned int us = timeout_ms * 1000;
    for(unsigned int t = 0; t < us; t += 10){
        unsigned int irq = EMMC_INTERRUPT;
        if(irq & INT_ERROR){ EMMC_INTERRUPT = 0xFFFFFFFF; return 0; }
        if((irq & mask) == mask){ EMMC_INTERRUPT = mask; return 1; }
        delay_us(10);
    }
    return 0;
}

// wait for STATUS bits to clear
static int wait_status(unsigned int mask, unsigned int timeout_ms){
    unsigned int us = timeout_ms * 1000;
    for(unsigned int t = 0; t < us; t += 10){
        if(!(EMMC_STATUS & mask)) return 1;
        delay_us(10);
    }
    return 0;
}

// send one command; returns RESP0 or -1 on error
static int send_cmd(unsigned int cmd, unsigned int arg){
    unsigned int inhibit = SR_CMD_INHIBIT;
    if((cmd & CMD_ISDATA) || ((cmd >> 16 & 3) == 3))
        inhibit |= SR_DAT_INHIBIT;
    if(!wait_status(inhibit, 100)){ return -1; }

    EMMC_INTERRUPT = 0xFFFFFFFF;   // clear all
    EMMC_ARG1  = arg;
    EMMC_CMDTM = cmd;

    if(!wait_int(INT_CMD_DONE, 500)) return -1;
    return (int)EMMC_RESP0;
}

// ACMD = CMD55 then cmd
static int send_acmd(unsigned int cmd, unsigned int arg){
    if(send_cmd(CMD(55) | R1, sd_rca << 16) < 0) return -1;
    return send_cmd(cmd, arg);
}

// ── public API ────────────────────────────────────────────────────────────────

int sd_ready(void){ return sd_type != 0; }

int sd_init(void){
    sd_type = 0;
    sd_rca  = 0;

    // ── 1. reset controller ───────────────────────────────────────────────────
    EMMC_CONTROL0 = 0;
    EMMC_CONTROL1 = C1_SRST_HC;
    if(!wait_status(C1_SRST_HC, 100)){
        uart_puts("[sd] controller reset timeout\n"); return -1;
    }

    // ── 2. start internal clock at ~400 kHz identification speed ──────────────
    // divisor field: [15:8] = lower 8 bits, [7:6] = upper 2 bits
    // 0x80 → divides base clock by 256 → well under 400 kHz
    EMMC_CONTROL1 = C1_CLK_INTLEN | C1_TOUNIT_MAX | (0x80 << 8);
    delay_us(1000);
    if(!wait_status(0, 10)){ /* non-blocking wait */ }
    // poll CLK_STABLE
    for(int i = 0; i < 10000; i++){
        if(EMMC_CONTROL1 & C1_CLK_STABLE) break;
        delay_us(10);
    }
    if(!(EMMC_CONTROL1 & C1_CLK_STABLE)){
        uart_puts("[sd] clock stable timeout\n"); return -1;
    }
    EMMC_CONTROL1 |= C1_CLK_EN;
    delay_us(2000);

    // interrupts go to EMMC_INTERRUPT register (not CPU)
    EMMC_IRPT_EN   = 0;
    EMMC_IRPT_MASK = 0xFFFFFFFF;

    // ── 3. card identification sequence ──────────────────────────────────────
    // CMD0: reset to idle
    send_cmd(CMD(0) | RSPNS_NONE, 0);
    delay_us(2000);

    // CMD8: check voltage; 0x1AA = 3.3V range + check pattern 0xAA
    int r = send_cmd(CMD(8) | RSPNS_48 | CMD_CRCCHK | CMD_IXCHK, 0x1AA);
    int v2 = (r > 0 && (r & 0xFF) == 0xAA);

    // ACMD41: initialise, read OCR; HCS=1 for SDHC support, XPC=1 max perf
    unsigned int ocr = 0;
    unsigned int arg = v2 ? 0x50FF8000u : 0x00FF8000u;
    for(int i = 0; i < 150; i++){
        r = send_acmd(CMD(41) | RSPNS_48, arg); // no CRC/IX check for ACMD41
        if(r < 0) break;
        ocr = (unsigned int)r;
        if(ocr >> 31) break;       // busy bit cleared = card ready
        delay_us(10000);
    }
    if(!(ocr >> 31)){
        uart_puts("[sd] ACMD41 timeout (card not ready)\n"); return -1;
    }
    sd_type = v2 ? ((ocr >> 30 & 1) ? 3 : 2) : 1; // HC=3, SC=2, v1=1

    // CMD2: get CID (136-bit response, we don't use it)
    send_cmd(CMD(2) | RSPNS_136, 0);

    // CMD3: get RCA
    r = send_cmd(CMD(3) | R1, 0);
    if(r < 0){ uart_puts("[sd] CMD3 failed\n"); return -1; }
    sd_rca = (unsigned int)r >> 16;

    // ── 4. switch to data transfer clock (~25 MHz; divisor 2) ────────────────
    EMMC_CONTROL1 = (EMMC_CONTROL1 & ~(0x3FFu << 6)) | (2u << 8);
    delay_us(1000);

    // CMD7: select card
    if(send_cmd(CMD(7) | RSPNS_48B | CMD_CRCCHK | CMD_IXCHK, sd_rca << 16) < 0){
        uart_puts("[sd] CMD7 failed\n"); return -1;
    }

    // CMD16: set block length to 512 (mandatory for SDv1, harmless for v2)
    if(send_cmd(CMD(16) | R1, SD_BLOCK_SIZE) < 0){
        uart_puts("[sd] CMD16 failed\n"); return -1;
    }

    uart_puts("[sd] ready (type=SD");
    char t[2] = { "?v1SC"[sd_type], '\0' };
    uart_puts(t);
    uart_puts(")\n");
    return 0;
}

int sd_read(unsigned int lba, unsigned char *buf, unsigned int count){
    if(!sd_type || !count) return -1;

    // SDHC/XC (type 3) address by block; older cards by byte
    unsigned int addr = (sd_type == 3) ? lba : lba * SD_BLOCK_SIZE;

    EMMC_BLKSIZECNT = (count << 16) | SD_BLOCK_SIZE;

    unsigned int cmd;
    if(count == 1){
        cmd = CMD(17) | R1 | CMD_ISDATA | TM_DAT_READ;
    } else {
        cmd = CMD(18) | R1 | CMD_ISDATA | TM_DAT_READ
            | TM_MULTI | TM_BLKCNT_EN | TM_AUTOCMD12;
    }
    if(send_cmd(cmd, addr) < 0) return -1;

    unsigned int *p = (unsigned int *)buf;
    for(unsigned int b = 0; b < count; b++){
        if(!wait_int(INT_READ_RDY, 1000)) return -1;
        for(unsigned int i = 0; i < SD_BLOCK_SIZE / 4; i++)
            *p++ = EMMC_DATA;
    }
    if(count > 1 && !wait_int(INT_DATA_DONE, 2000)) return -1;
    return 0;
}

int sd_write(unsigned int lba, const unsigned char *buf, unsigned int count){
    if(!sd_type || !count) return -1;

    unsigned int addr = (sd_type == 3) ? lba : lba * SD_BLOCK_SIZE;

    EMMC_BLKSIZECNT = (count << 16) | SD_BLOCK_SIZE;

    unsigned int cmd;
    if(count == 1){
        cmd = CMD(24) | R1 | CMD_ISDATA;
    } else {
        cmd = CMD(25) | R1 | CMD_ISDATA
            | TM_MULTI | TM_BLKCNT_EN | TM_AUTOCMD12;
    }
    if(send_cmd(cmd, addr) < 0) return -1;

    const unsigned int *p = (const unsigned int *)buf;
    for(unsigned int b = 0; b < count; b++){
        if(!wait_int(INT_WRITE_RDY, 1000)) return -1;
        for(unsigned int i = 0; i < SD_BLOCK_SIZE / 4; i++)
            EMMC_DATA = *p++;
    }
    if(!wait_int(INT_DATA_DONE, 2000)) return -1;
    return 0;
}
