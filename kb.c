//Hudson Strauss
#include "kb.h"
#include "mailbox.h"
#include "uart.h"
#include "timer.h"
#include "string.h"

// ── BCM2711 DWC2 USB2 OTG host controller ────────────────────────────────────
// Synopsys DesignWare USB 2.0 OTG core, peripheral base + 0x980000
#define DWC2_BASE       0xFE980000UL
#define R(off)          (*(volatile unsigned int *)(DWC2_BASE + (off)))

// Global core registers
#define GOTGCTL         R(0x000)
#define GAHBCFG         R(0x008)
#define GUSBCFG         R(0x00C)
#define GRSTCTL         R(0x010)
#define GINTSTS         R(0x014)
#define GINTMSK         R(0x018)
#define GRXSTSP         R(0x020)   // RX status FIFO pop
#define GRXFSIZ         R(0x024)
#define GNPTXFSIZ       R(0x028)   // [31:16]=size,[15:0]=start (words)
#define GNPTXSTS        R(0x02C)   // [31:16]=words avail in NP TX FIFO

// Host registers
#define HCFG            R(0x400)
#define HFNUM           R(0x408)   // [15:0]=frame number
#define HPRT            R(0x440)

// Per-channel registers  (ch = 0..7, stride 0x20)
#define HCCHAR(n)       R(0x500 + (n)*0x20)
#define HCSPLT(n)       R(0x504 + (n)*0x20)
#define HCINT(n)        R(0x508 + (n)*0x20)
#define HCINTMSK(n)     R(0x50C + (n)*0x20)
#define HCTSIZ(n)       R(0x510 + (n)*0x20)

// Data FIFOs: TX uses DFIFO(ch), RX always read from DFIFO(0)
#define DFIFO(n)        R(0x1000 + (n)*0x1000)

// ── HPRT bit positions ────────────────────────────────────────────────────────
#define HPRT_CONNSTS    (1u << 0)
#define HPRT_CONNDET    (1u << 1)   // w1c
#define HPRT_ENA        (1u << 2)
#define HPRT_ENCHNG     (1u << 3)   // w1c
#define HPRT_OVRCURRCHNG (1u<<5)    // w1c
#define HPRT_RST        (1u << 8)
#define HPRT_PWR        (1u << 12)
#define HPRT_SPD_SHIFT  17
#define HPRT_SPD_MASK   (3u << HPRT_SPD_SHIFT)
// mask of w1c bits — never write these 1 unless intentionally clearing them
#define HPRT_W1C_MASK   (HPRT_CONNDET | HPRT_ENCHNG | HPRT_OVRCURRCHNG | (1u<<6))

// ── HCCHAR field positions ────────────────────────────────────────────────────
// [10:0]  MPS      max packet size
// [14:11] EPNUM    endpoint number
// [15]    EPDIR    0=OUT 1=IN
// [19:18] EPTYPE   0=ctrl 1=isoc 2=bulk 3=intr
// [28:22] DEVADDR  device address
// [29]    ODDFRM   odd frame (for periodic)
// [30]    CHDIS    disable channel
// [31]    CHENA    enable channel
#define HCCHAR_IN       (1u << 15)
#define HCCHAR_LSDEV    (1u << 17)
#define HCCHAR_CTL      (0u << 18)
#define HCCHAR_INTR     (3u << 18)
#define HCCHAR_ODDFRM   (1u << 29)
#define HCCHAR_CHDIS    (1u << 30)
#define HCCHAR_CHENA    (1u << 31)

// ── HCTSIZ PID field [30:29] ─────────────────────────────────────────────────
#define PID_DATA0       (0u << 29)
#define PID_DATA1       (2u << 29)
#define PID_SETUP       (3u << 29)

// ── HCINT bits ────────────────────────────────────────────────────────────────
#define HCINT_XFERCOMPL (1u << 0)
#define HCINT_CHHLTD    (1u << 1)
#define HCINT_STALL     (1u << 3)
#define HCINT_NAK       (1u << 4)
#define HCINT_XACTERR   (1u << 7)
#define HCINT_BBLERR    (1u << 8)
#define HCINT_ACK       (1u << 5)

#define HCINT_COMMON_MASK (HCINT_XFERCOMPL | HCINT_CHHLTD | HCINT_STALL | \
                           HCINT_NAK | HCINT_XACTERR | HCINT_BBLERR | HCINT_ACK)

// ── GINTSTS bits ─────────────────────────────────────────────────────────────
#define GINTSTS_RXFLVL  (1u << 4)   // RX FIFO non-empty

// ── driver state ──────────────────────────────────────────────────────────────
static int kb_addr   = 0;   // assigned USB device address (0 = not ready)
static int kb_ep     = 1;   // interrupt IN endpoint number
static int kb_mps    = 8;   // interrupt endpoint max packet size
static int kb_toggle = 0;   // data toggle for interrupt IN (0=DATA0, 1=DATA1)
static int kb_low_speed = 0;
static unsigned int kb_last_out_intr = 0;
static unsigned int kb_last_in_intr  = 0;
static unsigned int kb_last_in_ci    = 0;
static unsigned int kb_last_grx      = 0;
static int kb_last_in_received       = 0;

static void usb_dump_status(const char *tag){
    uart_puts("[kb] ");
    uart_puts(tag);
    uart_puts(": out_intr=");
    uart_puthex(kb_last_out_intr);
    uart_puts(" in_intr=");
    uart_puthex(kb_last_in_intr);
    uart_puts(" in_ci=");
    uart_puthex(kb_last_in_ci);
    uart_puts(" grx=");
    uart_puthex(kb_last_grx);
    uart_puts(" recv=");
    uart_puthex((unsigned long)kb_last_in_received);
    uart_puts(" hprt=");
    uart_puthex(HPRT);
    uart_puts(" hcchar0=");
    uart_puthex(HCCHAR(0));
    uart_puts(" hctsiz0=");
    uart_puthex(HCTSIZ(0));
    uart_puts("\n");
}

static const char *port_speed_name(unsigned int spd){
    switch(spd){
        case 0: return "high";
        case 1: return "full";
        case 2: return "low";
        default: return "unknown";
    }
}

// ASCII ring buffer
#define KB_BUF  32
static char kb_ring[KB_BUF];
static int  kb_head = 0, kb_tail = 0;

static void kb_push(char c){
    int next = (kb_head + 1) % KB_BUF;
    if(next != kb_tail){ kb_ring[kb_head] = c; kb_head = next; }
}

// Previous HID report (to detect new key presses vs held keys)
static unsigned char prev_rep[8];

// ── HID usage → ASCII (US QWERTY) ────────────────────────────────────────────
static const char hid_lo[0x40] = {
    /* 00 */ 0,    /* 01 */ 0,    /* 02 */ 0,    /* 03 */ 0,
    /* 04 a */ 'a',/* 05 b */ 'b',/* 06 c */ 'c',/* 07 d */ 'd',
    /* 08 e */ 'e',/* 09 f */ 'f',/* 0A g */ 'g',/* 0B h */ 'h',
    /* 0C i */ 'i',/* 0D j */ 'j',/* 0E k */ 'k',/* 0F l */ 'l',
    /* 10 m */ 'm',/* 11 n */ 'n',/* 12 o */ 'o',/* 13 p */ 'p',
    /* 14 q */ 'q',/* 15 r */ 'r',/* 16 s */ 's',/* 17 t */ 't',
    /* 18 u */ 'u',/* 19 v */ 'v',/* 1A w */ 'w',/* 1B x */ 'x',
    /* 1C y */ 'y',/* 1D z */ 'z',/* 1E 1 */ '1',/* 1F 2 */ '2',
    /* 20 3 */ '3',/* 21 4 */ '4',/* 22 5 */ '5',/* 23 6 */ '6',
    /* 24 7 */ '7',/* 25 8 */ '8',/* 26 9 */ '9',/* 27 0 */ '0',
    /* 28 Enter */'\r',/* 29 Esc */0x1b,/* 2A BkSp */0x7f,/* 2B Tab */'\t',
    /* 2C Spc */ ' ',/* 2D - */ '-', /* 2E = */ '=', /* 2F [ */ '[',
    /* 30 ] */ ']', /* 31 \ */ '\\',/* 32 */  0,   /* 33 ; */ ';',
    /* 34 ' */ '\'',/* 35 ` */ '`', /* 36 , */ ',', /* 37 . */ '.',
    /* 38 / */ '/', /* 39..3F */ 0,0,0,0,0,0,0,
};
static const char hid_hi[0x40] = {
    0,    0,    0,    0,
    'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',
    'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',
    'Q',  'R',  'S',  'T',  'U',  'V',  'W',  'X',
    'Y',  'Z',  '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',
    '\r', 0x1b, 0x7f, '\t', ' ',  '_',  '+',  '{',
    '}',  '|',  0,    ':',  '"',  '~',  '<',  '>',
    '?',  0,    0,    0,    0,    0,    0,    0,
};

static void hid_decode(const unsigned char *rep){
    // modifier byte: bit1=L-shift, bit5=R-shift
    int shift = (rep[0] & 0x22) != 0;
    for(int i = 2; i < 8; i++){
        unsigned char code = rep[i];
        if(!code) continue;
        // ignore if key was already held in previous report
        int held = 0;
        for(int j = 2; j < 8; j++) if(prev_rep[j] == code){ held = 1; break; }
        if(held) continue;
        if(code < 0x40){
            char c = shift ? hid_hi[code] : hid_lo[code];
            if(c) kb_push(c);
        }
    }
    memcpy(prev_rep, rep, 8);
}

// ── FIFO helpers ──────────────────────────────────────────────────────────────

// Write 'len' bytes to the NP TX FIFO for channel 'ch'
static void fifo_tx(int ch, const unsigned char *data, int len){
    int words = (len + 3) >> 2;
    // wait for TX FIFO space
    for(int t = 0; t < 200000; t++){
        if(((GNPTXSTS >> 16) & 0xFFFFu) >= (unsigned)words) break;
        delay_us(1);
    }
    for(int i = 0; i < words; i++){
        unsigned int w = 0;
        for(int b = 0; b < 4 && i*4+b < len; b++)
            w |= ((unsigned int)data[i*4+b]) << (b*8);
        DFIFO(ch) = w;
    }
}

// Read 'len' bytes from the shared RX FIFO (always at DFIFO(0))
static void fifo_rx(unsigned char *buf, int len){
    int words = (len + 3) >> 2;
    for(int i = 0; i < words; i++){
        unsigned int w = DFIFO(0);
        for(int b = 0; b < 4 && i*4+b < len; b++)
            buf[i*4+b] = (unsigned char)(w >> (b*8));
    }
}

// Drain 'len' bytes from the RX FIFO (discarding data)
static void fifo_drain(int len){
    int words = (len + 3) >> 2;
    for(int i = 0; i < words; i++) (void)DFIFO(0);
}

// Wait for any HCINT bit; returns value or 0 on timeout (~500ms)
static unsigned int ch_wait(int ch){
    for(int t = 0; t < 500000; t++){
        unsigned int v = HCINT(ch);
        if(v) return v;
        delay_us(1);
    }
    return 0;
}

// ── USB transfer primitives ───────────────────────────────────────────────────

// Send 'len' bytes OUT (or ZLP if len==0)
// ep_type: HCCHAR_CTL or HCCHAR_INTR
// pid:     PID_SETUP / PID_DATA0 / PID_DATA1
// Returns 0 on success, -1 on error
static int usb_out(int ch, int addr, int ep, unsigned int ep_type, int mps,
                   unsigned int pid, const unsigned char *data, int len){
    HCINT(ch)  = 0xFFFFFFFF;
    HCINTMSK(ch) = HCINT_COMMON_MASK;
    HCSPLT(ch) = 0;
    kb_last_out_intr = 0;

    int pkts = len ? (len + mps - 1) / mps : 1;
    HCTSIZ(ch) = ((unsigned)len & 0x7FFFFu) |
                 ((unsigned)(pkts & 0x3FF) << 19) | pid;

    unsigned int hcchar = ((unsigned)(mps & 0x7FF)) |
                          ((unsigned)(ep & 0xF) << 11) |
                          /* OUT: EPDir bit = 0 */
                          ep_type |
                          ((unsigned)(addr & 0x7F) << 22);
    if(kb_low_speed) hcchar |= HCCHAR_LSDEV;

    if(len > 0) fifo_tx(ch, data, len);

    HCCHAR(ch) = hcchar | HCCHAR_CHENA;

    unsigned int intr = ch_wait(ch);
    kb_last_out_intr = intr;
    HCINT(ch) = 0xFFFFFFFF;
    return (intr & HCINT_XFERCOMPL) ? 0 : -1;
}

// Receive up to 'len' bytes IN
// Returns bytes received, -2 on NAK (no data, retry), -1 on error
static int usb_in(int ch, int addr, int ep, unsigned int ep_type, int mps,
                  unsigned int pid, unsigned char *buf, int len){
    HCINT(ch)  = 0xFFFFFFFF;
    HCINTMSK(ch) = HCINT_COMMON_MASK;
    HCSPLT(ch) = 0;
    kb_last_in_intr = 0;
    kb_last_in_ci = 0;
    kb_last_grx = 0;
    kb_last_in_received = 0;

    int pkts = len ? (len + mps - 1) / mps : 1;

    unsigned int hcchar = ((unsigned)(mps & 0x7FF)) |
                          ((unsigned)(ep & 0xF) << 11) |
                          HCCHAR_IN | ep_type |
                          ((unsigned)(addr & 0x7F) << 22);
    if(kb_low_speed) hcchar |= HCCHAR_LSDEV;

    // For interrupt endpoints set ODDFRM for correct microframe scheduling
    if(ep_type == HCCHAR_INTR && (HFNUM & 1)) hcchar |= HCCHAR_ODDFRM;

    HCTSIZ(ch) = ((unsigned)(pkts * mps) & 0x7FFFFu) |
                 ((unsigned)(pkts & 0x3FF) << 19) | pid;
    HCCHAR(ch) = hcchar | HCCHAR_CHENA;

    int received = 0;

    for(int t = 0; t < 500000; t++){
        if(GINTSTS & GINTSTS_RXFLVL){
            unsigned int grx    = GRXSTSP;
            kb_last_grx = grx;
            int          grx_ch  = (int)(grx & 0xF);
            int          grx_cnt = (int)((grx >> 4) & 0x7FF);
            int          grx_sts = (int)((grx >> 17) & 0xF);

            if(grx_ch != ch){
                // stray data — drain it
                if(grx_cnt > 0) fifo_drain(grx_cnt);
                continue;
            }
            if(grx_sts == 2 && grx_cnt > 0){  // IN data packet received
                int want = (len - received);
                if(grx_cnt <= want){
                    fifo_rx(buf + received, grx_cnt);
                    received += grx_cnt;
                    kb_last_in_received = received;
                } else {
                    fifo_rx(buf + received, want);
                    fifo_drain(grx_cnt - want);
                    received += want;
                    kb_last_in_received = received;
                }
            } else if(grx_sts == 3){           // IN transfer complete
                break;
            } else if(grx_cnt > 0){            // other status with data — drain
                fifo_drain(grx_cnt);
            }
            continue;
        }

        // check channel interrupt flags
        unsigned int ci = HCINT(ch);
        if(ci){
            kb_last_in_ci = ci;
            HCINT(ch) = 0xFFFFFFFF;
            if(ci & HCINT_NAK)       return -2;
            if(ci & HCINT_XFERCOMPL) return received;
            if(ci & HCINT_CHHLTD)    return received > 0 ? received : -1;
            return -1;
        }
        delay_us(1);
    }

    unsigned int intr = ch_wait(ch);
    kb_last_in_intr = intr;
    HCINT(ch) = 0xFFFFFFFF;
    if(intr & (HCINT_XFERCOMPL | HCINT_CHHLTD)) return received;
    return -1;
}

// ── USB control transfer helpers ──────────────────────────────────────────────

// Device-to-host control (GET_DESCRIPTOR etc.): SETUP OUT → DATA IN → STATUS OUT ZLP
static int ctrl_in(int addr, const unsigned char *setup,
                   unsigned char *buf, int len){
    int mps = (addr == 0) ? 8 : 64;
    if(usb_out(0, addr, 0, HCCHAR_CTL, mps, PID_SETUP, setup, 8) < 0){
        usb_dump_status("ctrl_in setup failed");
        return -1;
    }
    int n = 0;
    if(len > 0){
        n = usb_in(0, addr, 0, HCCHAR_CTL, mps, PID_DATA1, buf, len);
        if(n < 0){
            usb_dump_status("ctrl_in data failed");
            return -1;
        }
    }
    // STATUS: ZLP OUT
    if(usb_out(0, addr, 0, HCCHAR_CTL, mps, PID_DATA1, 0, 0) < 0)
        usb_dump_status("ctrl_in status failed");
    return n;
}

// Host-to-device control with no data (SET_ADDRESS, SET_CONFIGURATION, etc.)
// SETUP OUT → STATUS IN ZLP
static int ctrl_out(int addr, const unsigned char *setup){
    int mps = (addr == 0) ? 8 : 64;
    if(usb_out(0, addr, 0, HCCHAR_CTL, mps, PID_SETUP, setup, 8) < 0){
        usb_dump_status("ctrl_out setup failed");
        return -1;
    }
    // STATUS: ZLP IN
    unsigned char dummy[8];
    if(usb_in(0, addr, 0, HCCHAR_CTL, mps, PID_DATA1, dummy, 0) < 0)
        usb_dump_status("ctrl_out status failed");
    return 0;
}

// ── USB enumeration ───────────────────────────────────────────────────────────

static int enumerate_keyboard(void){
    unsigned char buf[256];
    unsigned char setup[8];

    // GET_DESCRIPTOR(Device, 18 bytes) — verify device exists, get ep0 MPS
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x01;
    setup[4]=0x00; setup[5]=0x00; setup[6]=18;   setup[7]=0x00;
    int n = -1;
    for(int attempt = 0; attempt < 8; attempt++){
        n = ctrl_in(0, setup, buf, 18);
        if(n >= 8) break;
        delay_us(10000);
    }
    if(n < 8){
        uart_puts("[kb] GET_DESCRIPTOR(Device) failed\n");
        return -1;
    }

    // SET_ADDRESS(1)
    setup[0]=0x00; setup[1]=0x05; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    ctrl_out(0, setup);
    delay_us(5000);   // device needs time to latch new address
    kb_addr = 1;

    // GET_DESCRIPTOR(Config) — parse to find HID interrupt IN endpoint
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x02;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0xFF; setup[7]=0x00;
    n = ctrl_in(1, setup, buf, 255);
    if(n < 9){
        uart_puts("[kb] GET_DESCRIPTOR(Config) failed\n");
        return -1;
    }

    // Walk descriptor list looking for interrupt IN endpoint
    int ep_found = 0;
    int i = 0;
    while(i + 2 <= n){
        int dlen  = buf[i];
        int dtype = buf[i+1];
        if(dlen < 2) break;
        if(dtype == 0x05 && dlen >= 7){   // Endpoint Descriptor
            int ep_addr = buf[i+2];
            int ep_attr = buf[i+3];
            int ep_pkt  = (int)(buf[i+4]) | ((int)(buf[i+5]) << 8);
            if((ep_addr & 0x80) && (ep_attr & 3) == 3){ // interrupt IN
                kb_ep  = ep_addr & 0x0F;
                kb_mps = (ep_pkt > 8) ? 8 : ep_pkt; // boot protocol = 8 bytes
                ep_found = 1;
                break;
            }
        }
        i += dlen;
    }
    if(!ep_found){
        uart_puts("[kb] no HID interrupt endpoint found\n");
        return -1;
    }

    // SET_CONFIGURATION(1)
    setup[0]=0x00; setup[1]=0x09; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    ctrl_out(1, setup);
    delay_us(5000);

    // HID SET_PROTOCOL(0) = boot protocol — gives fixed 8-byte reports
    // bmRequestType=0x21 (class, interface, H→D)
    // bRequest=0x0B, wValue=0 (boot), wIndex=0 (interface), wLength=0
    setup[0]=0x21; setup[1]=0x0B; setup[2]=0x00; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    ctrl_out(1, setup);

    uart_puts("[kb] HID keyboard ready (boot protocol)\n");
    return 0;
}

// ── DWC2 core and port init ───────────────────────────────────────────────────

static int usb_power_on(void){
    // Turn on USB power via VideoCore mailbox property interface
    // Tag 0x00028001 = SET_POWER_STATE
    // Device ID 3 = USB HCD, State 3 = on + wait for stable
    __attribute__((aligned(16))) volatile unsigned int mbox[8] = {
        8 * 4,        // total buffer size in bytes
        0x00000000,   // request code
        0x00028001,   // tag: SET_POWER_STATE
        8,            // tag value buffer size
        0,            // tag indicator (filled by VC)
        3,            // device ID: USB HCD
        3,            // state: on (bit0) + wait (bit1)
        0x00000000,   // end tag
    };
    if(!mbox_call(MBOX_CH_PROP, mbox)){
        uart_puts("[kb] USB power mailbox failed\n");
        return -1;
    }
    return 0;
}

static int dwc2_init(void){
    // Core soft reset: wait for AHB idle then assert CSftRst
    for(int t = 0; t < 200000 && !(GRSTCTL & (1u<<31)); t++) delay_us(1);
    GRSTCTL = 1u;
    for(int t = 0; t < 200000 && (GRSTCTL & 1u); t++) delay_us(1);
    if(GRSTCTL & 1u){ uart_puts("[kb] DWC2 soft reset timeout\n"); return -1; }
    delay_us(3000);

    // Disable all interrupts (polling mode)
    GAHBCFG &= ~1u;
    GINTMSK  = 0;
    GINTSTS  = 0xFFFFFFFF; // clear pending

    // Force host mode (bit 29 of GUSBCFG)
    GUSBCFG = (GUSBCFG & ~((1u<<28)|(1u<<29))) | (1u << 29);
    delay_us(25000); // spec requires 25ms for mode switch

    // Configure RX and NP-TX FIFOs (sizes in 32-bit words)
    // RX: 256 words from offset 0; NP-TX: 256 words from offset 256
    GRXFSIZ   = 0x0100u;
    GNPTXFSIZ = (0x0100u << 16) | 0x0100u;

    // BCM2711/QEMU uses the high-speed PHY path, so both high-speed and
    // full-speed devices use the 30/60 MHz setting here. Only low-speed
    // devices need the 6 MHz clock.
    HCFG = (HCFG & ~3u) | 0u;  // FSLSPClkSel = 00 (30/60 MHz)

    // Re-enable the core after setup. We still poll status registers instead of
    // taking IRQs, but some host-state transitions do not progress cleanly with
    // the global interrupt gate left disabled.
    GAHBCFG |= 1u;

    return 0;
}

static int port_init(void){
    // Power on the root hub port
    unsigned int hprt = HPRT & ~HPRT_W1C_MASK;
    hprt |= HPRT_PWR;
    HPRT = hprt;
    delay_us(50000);  // 50ms power stabilization

    // Wait up to 2 seconds for device connect
    int connected = 0;
    for(int t = 0; t < 2000; t++){
        if(HPRT & HPRT_CONNSTS){ connected = 1; break; }
        delay_us(1000);
    }
    if(!connected){
        uart_puts("[kb] no USB device on root port\n");
        return -1;
    }

    // Clear connect-detected sticky bit
    HPRT = (HPRT & ~HPRT_W1C_MASK) | HPRT_CONNDET;
    delay_us(200);

    // Issue port reset for 50ms (USB spec minimum is 10ms for root hub)
    HPRT = (HPRT & ~HPRT_W1C_MASK) | HPRT_RST;
    delay_us(50000);
    HPRT = HPRT & ~(HPRT_W1C_MASK | HPRT_RST);
    delay_us(100000);  // allow full-speed devices extra time to settle after reset

    // Wait for port to become enabled
    for(int t = 0; t < 200; t++){
        if(HPRT & HPRT_ENA) break;
        delay_us(1000);
    }
    if(!(HPRT & HPRT_ENA)){
        uart_puts("[kb] root port failed to enable: HPRT=");
        uart_puthex(HPRT);
        uart_puts("\n");
        return -1;
    }
    // Clear enable-changed bit
    HPRT = (HPRT & ~HPRT_W1C_MASK) | HPRT_ENCHNG;

    unsigned int spd = (HPRT & HPRT_SPD_MASK) >> HPRT_SPD_SHIFT;
    kb_low_speed = (spd == 2u);
    // High-speed PHY: 30/60 MHz for high/full-speed, 6 MHz for low-speed.
    HCFG = (HCFG & ~3u) | (kb_low_speed ? 2u : 0u);
    uart_puts("[kb] root port speed: ");
    uart_puts(port_speed_name(spd));
    uart_puts("\n");

    return 0;
}

// ── public API ────────────────────────────────────────────────────────────────

int kb_init(void){
    memset(prev_rep, 0, 8);
    kb_addr = kb_ep = kb_toggle = 0;
    kb_mps  = 8;
    kb_head = kb_tail = 0;
    kb_low_speed = 0;

    usb_power_on();   // best-effort; QEMU may not require it
    delay_us(10000);

    if(dwc2_init()  < 0) return -1;
    if(port_init()  < 0) return -1;
    if(enumerate_keyboard() < 0) return -1;
    return 0;
}

int kb_ready(void){
    return kb_head != kb_tail;
}

char kb_getc(void){
    if(kb_head == kb_tail) return 0;
    char c = kb_ring[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF;
    return c;
}

void kb_poll(void){
    if(!kb_addr) return;

    unsigned char rep[8];
    unsigned int  pid = kb_toggle ? PID_DATA1 : PID_DATA0;

    int n = usb_in(1, kb_addr, kb_ep, HCCHAR_INTR, kb_mps, pid, rep, 8);
    if(n == 8){
        kb_toggle ^= 1;
        hid_decode(rep);
    }
    // NAK (-2) = no new data, don't toggle — just retry next poll
    // Any other error: also don't toggle
}
