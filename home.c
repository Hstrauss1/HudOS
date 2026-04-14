//Hudson Strauss
#include "home.h"
#include "fb.h"
#include "uart.h"
#include "task.h"
#include "timer.h"
#include "alloc.h"
#include "string.h"
#include "version.h"

// ── extern state ──────────────────────────────────────────────────────────────
extern task_t tasks[];
extern int    current_task;

// ── color palette ─────────────────────────────────────────────────────────────
#define C_BG        0xFF1A1D21u  // near-black desktop
#define C_BAR       0xFF252830u  // top/bottom chrome bar
#define C_TEXT      0xFFDDE1E6u  // primary text
#define C_DIM       0xFF6C7480u  // secondary / label text
#define C_ACCENT    0xFF4D9DEAu  // blue accent (OS name, section heads)
#define C_GREEN     0xFF42A862u  // running
#define C_ORANGE    0xFFD4A017u  // sleeping
#define C_RED       0xFFCC4444u  // dead
#define C_TASKBTN   0xFF32363Fu  // inactive task button
#define C_TASKBTN_A 0xFF2D6A9Fu  // active task button

// ── layout ────────────────────────────────────────────────────────────────────
#define TOPBAR_H  24u   // status bar at top
#define TASKBAR_H 22u   // running-tasks bar at bottom

// ── task state ────────────────────────────────────────────────────────────────
static volatile int home_running = 0;
static          int home_task_id = -1;

// ── string helpers ────────────────────────────────────────────────────────────

static int sapp(char *b, int p, const char *s){
    while(*s) b[p++] = *s++;
    return p;
}

static int napp(char *b, int p, unsigned long n){
    char tmp[22]; itoa((int)n, tmp, 22);
    int i = 0; while(tmp[i]) b[p++] = tmp[i++];
    return p;
}

static int uptime_str(char *b, int p){
    unsigned long t = timer_get_ticks();
    unsigned long s = t / 1000000, f = (t / 1000) % 1000;
    p = napp(b, p, s);
    b[p++] = '.';
    if(f < 100) b[p++] = '0';
    if(f < 10)  b[p++] = '0';
    p = napp(b, p, f);
    b[p++] = 's';
    b[p]   = '\0';
    return p;
}

// ── topbar ────────────────────────────────────────────────────────────────────
// Draws the status bar at y=0. Only touches rows [0, TOPBAR_H).
// Safe to call from the refresh task without disturbing the terminal region.

static void draw_topbar(unsigned int W){
    fb_fill_rect(0, 0, W, TOPBAR_H, C_BAR);
    // bottom border
    fb_fill_rect(0, TOPBAR_H - 1, W, 1, 0xFF363C47u);

    // ── left: OS name in accent colour ───────────────────────────────────────
    fb_put_string(6, 8, HUDOS_NAME " v" HUDOS_VERSION, C_ACCENT, C_BAR);

    // ── right cluster: uptime | tasks | memory ───────────────────────────────
    // Build right-side string
    char r[80]; int rp = 0;

    // uptime
    rp = sapp(r, rp, "up ");
    rp = uptime_str(r, rp);

    rp = sapp(r, rp, "  |  ");

    // task count (non-dead/unused)
    int alive = 0;
    for(int i = 0; i < MAX_TASKS; i++)
        if(tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DEAD)
            alive++;
    rp = sapp(r, rp, "tasks:");
    rp = napp(r, rp, (unsigned long)alive);

    rp = sapp(r, rp, "  |  ");

    // free memory in MB
    unsigned long free_kb = alloc_free() / 1024;
    rp = napp(r, rp, free_kb / 1024);
    r[rp++] = '.';
    unsigned long frac = (free_kb % 1024) * 10 / 1024;
    r[rp++] = (char)('0' + frac);
    rp = sapp(r, rp, " MB free");
    r[rp] = '\0';

    unsigned int rw = (unsigned int)k_strlen(r) * 8;
    fb_put_string(W - rw - 8, 8, r, C_DIM, C_BAR);
}

// ── taskbar ───────────────────────────────────────────────────────────────────
// Draws the running-tasks bar at y=bottom_y. Only touches [bottom_y, FH).

static void draw_taskbar(unsigned int W, unsigned int bottom_y){
    fb_fill_rect(0, bottom_y, W, TASKBAR_H, C_BAR);
    // top border
    fb_fill_rect(0, bottom_y, W, 1, 0xFF363C47u);

    unsigned int bx = 4;
    for(int i = 0; i < MAX_TASKS; i++){
        if(tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD) continue;
        unsigned int nlen = (unsigned int)k_strlen(tasks[i].name);
        unsigned int bw   = nlen * 8 + 10;
        if(bx + bw + 4 > W - 96) break;
        unsigned int bc = (i == current_task) ? C_TASKBTN_A : C_TASKBTN;
        fb_fill_rect(bx, bottom_y + 3, bw, TASKBAR_H - 6, bc);

        // state dot
        unsigned int sc = C_DIM;
        if(tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_READY)  sc = C_GREEN;
        else if(tasks[i].state == TASK_SLEEPING) sc = C_ORANGE;
        else if(tasks[i].state == TASK_DEAD)     sc = C_RED;
        fb_fill_rect(bx + 3, bottom_y + 9, 4, 4, sc);

        fb_put_string(bx + 10, bottom_y + 7, tasks[i].name, C_TEXT, bc);
        bx += bw + 3;
    }

    // right: exception level
    fb_put_string(W - 40, bottom_y + 7, HUDOS_EL, C_DIM, C_BAR);
}

// ── chrome-only redraw ────────────────────────────────────────────────────────
// Called by the background refresh task every 500ms.
// Only redraws topbar and taskbar — the terminal region is never touched.

static void home_draw_chrome(void){
    unsigned int W  = fb_width();
    unsigned int FH = fb_height();
    if(W == 0 || FH == 0) return;
    draw_topbar(W);
    draw_taskbar(W, FH - TASKBAR_H);
}

// ── background refresh task ───────────────────────────────────────────────────

static void home_task_fn(void){
    while(home_running){
        home_draw_chrome();
        task_sleep_ms(500);
    }
}

// ── public API ────────────────────────────────────────────────────────────────

void home_start(void){
    if(home_running) return;
    if(fb_width() == 0) return;
    if(home_task_id >= 0){
        task_kill(home_task_id);
        home_task_id = -1;
    }
    home_running = 1;

    unsigned int W  = fb_width();
    unsigned int FH = fb_height();

    // ── 1. fill desktop background ────────────────────────────────────────────
    fb_fill_rect(0, 0, W, FH, C_BG);

    // ── 2. draw chrome ────────────────────────────────────────────────────────
    draw_topbar(W);
    draw_taskbar(W, FH - TASKBAR_H);

    // ── 3. set up full-width terminal region ──────────────────────────────────
    // Region sits between the two chrome bars with a 2px gap on each side.
    unsigned int term_y = TOPBAR_H + 2;
    unsigned int term_h = FH - TASKBAR_H - term_y - 2;
    unsigned int cols   = (W > 4) ? (W - 4) / 8 : 1;
    unsigned int rows   = term_h / 8;
    if(cols < 1) cols = 1;
    if(rows < 1) rows = 1;

    fb_console_init_region(2, term_y, cols, rows, C_TEXT, C_BG);

    // ── 4. redirect all uart output to the terminal ───────────────────────────
    uart_set_output_hook(fb_console_putc);

    // ── 5. spawn chrome refresh task ─────────────────────────────────────────
    home_task_id = task_create_named(home_task_fn, "home-ui");
    if(home_task_id < 0){
        home_running  = 0;
        home_task_id  = -1;
        uart_set_output_hook(0);
    }
}

void home_stop(void){
    home_running = 0;
    if(home_task_id >= 0)
        task_kill(home_task_id);
    home_task_id = -1;
    uart_set_output_hook(0);
    fb_console_disable();
    // restore plain background
    fb_fill_rect(0, 0, fb_width(), fb_height(), C_BG);
}

int home_active(void){
    return home_running;
}
