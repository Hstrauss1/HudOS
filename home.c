//Hudson Strauss
#include "home.h"
#include "fb.h"
#include "task.h"
#include "timer.h"
#include "alloc.h"
#include "string.h"
#include "version.h"

// ── extern state ──────────────────────────────────────────────────────────────
extern task_t tasks[];
extern int    current_task;

// ── color palette (visible dark theme) ───────────────────────────────────────
// All colors deliberately have enough brightness to show on screen
#define C_BG        0xFF1E2128u  // dark grey desktop
#define C_BAR       0xFF2C313Au  // slightly lighter bar
#define C_PANEL_L   0xFF252930u  // left panel
#define C_PANEL_R   0xFF21252Cu  // right panel
#define C_BORDER    0xFF4A5160u  // visible divider lines
#define C_TEXT      0xFFDDE1E6u  // bright white text
#define C_DIM       0xFF8B9196u  // grey secondary text
#define C_ACCENT    0xFF4D9DEAu  // blue accent
#define C_GREEN     0xFF42A862u  // running (green)
#define C_ORANGE    0xFFD4A017u  // sleeping (orange)
#define C_RED       0xFFCC4444u  // dead (red)
#define C_TASKBTN   0xFF363C47u  // task button bg
#define C_TASKBTN_A 0xFF2D6A9Fu  // active task button (blue)
#define C_LOGO_BG   0xFF2C3E5Au  // logo background strip

// ── fixed layout sizes ────────────────────────────────────────────────────────
#define TOPBAR_H    20u
#define TASKBAR_H   22u

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

// ── layout helpers ────────────────────────────────────────────────────────────

static void txt(unsigned int x, unsigned int y, const char *s, unsigned int fg, unsigned int bg){
	fb_put_string(x, y, s, fg, bg);
}

static void hrule(unsigned int y, unsigned int x0, unsigned int w){
	fb_fill_rect(x0, y, w, 1, C_BORDER);
}

static void vrule(unsigned int x, unsigned int y0, unsigned int h){
	fb_fill_rect(x, y0, 1, h, C_BORDER);
}

static void dot(int cx, int cy, unsigned int r, unsigned int col){
	for(int dy = -(int)r; dy <= (int)r; dy++)
		for(int dx = -(int)r; dx <= (int)r; dx++)
			if(dx*dx + dy*dy <= (int)(r*r))
				fb_put_pixel((unsigned int)(cx+dx), (unsigned int)(cy+dy), col);
}

static void pbar(unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                 unsigned long num, unsigned long den, unsigned int col){
	fb_fill_rect(x, y, w, h, C_TASKBTN);
	if(den > 0){
		unsigned int f = (unsigned int)((num * (unsigned long)w) / den);
		if(f > w) f = w;
		if(f > 0) fb_fill_rect(x, y, f, h, col);
	}
	// 1px border
	fb_fill_rect(x,       y,       w, 1, C_BORDER);
	fb_fill_rect(x,       y+h-1,   w, 1, C_BORDER);
	fb_fill_rect(x,       y,       1, h, C_BORDER);
	fb_fill_rect(x+w-1,   y,       1, h, C_BORDER);
}

// ── section draw functions ────────────────────────────────────────────────────

static void draw_topbar(unsigned int W){
	fb_fill_rect(0, 0, W, TOPBAR_H, C_BAR);
	hrule(TOPBAR_H, 0, W);
	// left: name
	txt(6, 6, HUDOS_NAME " v" HUDOS_VERSION, C_ACCENT, C_BAR);
	// right: uptime + EL
	char buf[40]; int p = sapp(buf, 0, "up ");
	p = uptime_str(buf, p);
	p = sapp(buf, p, "  " HUDOS_EL);
	buf[p] = '\0';
	unsigned int rw = (unsigned int)(k_strlen(buf)) * 8;
	txt(W - rw - 6, 6, buf, C_DIM, C_BAR);
}

static void draw_left(unsigned int sx, unsigned int ty, unsigned int ph){
	// background
	fb_fill_rect(0, ty, sx, ph, C_PANEL_L);

	// ── logo strip ──────────────────────────────────────
	unsigned int ly = ty + 12;
	fb_fill_rect(0, ly, sx, 18, C_LOGO_BG);
	// center "HudOS" on the strip
	unsigned int lw = (unsigned int)(k_strlen(HUDOS_NAME)) * 8;
	txt((sx - lw) / 2, ly + 5, HUDOS_NAME, C_TEXT, C_LOGO_BG);

	unsigned int iy = ly + 28;

	// subtitle
	const char *sub = "Bare Metal AArch64";
	txt((sx - (unsigned int)(k_strlen(sub))*8) / 2, iy, sub, C_DIM, C_PANEL_L);
	iy += 14;

	hrule(iy, 8, sx - 16);
	iy += 8;

	// ── stats ────────────────────────────────────────────
	unsigned int ix = 10;

	// uptime
	char ubuf[32]; int up = sapp(ubuf, 0, "up  ");
	up = uptime_str(ubuf, up); ubuf[up] = '\0';
	txt(ix, iy,      ubuf, C_TEXT, C_PANEL_L);

	// EL
	txt(ix, iy + 12, "EL  " HUDOS_EL, C_TEXT, C_PANEL_L);

	// task count
	int alive = 0;
	for(int i = 0; i < MAX_TASKS; i++)
		if(tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DEAD)
			alive++;
	char tbuf[32]; int tp = sapp(tbuf, 0, "tsk ");
	tp = napp(tbuf, tp, (unsigned long)alive); tbuf[tp] = '\0';
	txt(ix, iy + 24, tbuf, C_TEXT, C_PANEL_L);

	// heap used
	char hbuf[32]; int hp = sapp(hbuf, 0, "mem ");
	hp = napp(hbuf, hp, alloc_used() / 1024);
	hp = sapp(hbuf, hp, " KB"); hbuf[hp] = '\0';
	txt(ix, iy + 36, hbuf, C_TEXT, C_PANEL_L);

	// IRQ count
	char ibuf[32]; int ip = sapp(ibuf, 0, "irq ");
	ip = napp(ibuf, ip, timer_get_irq_count()); ibuf[ip] = '\0';
	txt(ix, iy + 48, ibuf, C_DIM, C_PANEL_L);

	iy += 68;
	hrule(iy, 8, sx - 16);
	iy += 8;

	// memory bar
	unsigned long used  = alloc_used();
	unsigned long avail = alloc_free() + used;
	if(avail == 0) avail = 1;
	unsigned int bw = (sx > 24) ? sx - 24 : 10;
	pbar(ix, iy, bw, 8, used, avail, C_ACCENT);

	char pbuf[24]; int pp = sapp(pbuf, 0, "blk ");
	pp = napp(pbuf, pp, alloc_free_blocks()); pbuf[pp] = '\0';
	txt(ix, iy + 12, pbuf, C_DIM, C_PANEL_L);
}

static void draw_right(unsigned int sx, unsigned int W, unsigned int ty, unsigned int ph,
                        unsigned int bottom_y){
	unsigned int x0 = sx + 1;
	unsigned int rw = (W > x0 + 1) ? W - x0 - 1 : 0;
	if(rw == 0) return;
	fb_fill_rect(x0, ty, rw, ph, C_PANEL_R);

	unsigned int tx = x0 + 8;

	// ── TASKS header ─────────────────────────────────────
	fb_fill_rect(x0, ty, rw, 16, C_BAR);
	txt(tx, ty + 4, "TASKS", C_ACCENT, C_BAR);
	hrule(ty + 16, x0, rw);

	static const char *snames[] = {"unused","ready","running","dead","sleeping"};
	static const unsigned int scols[] = {C_DIM, C_GREEN, C_GREEN, C_RED, C_ORANGE};

	unsigned int row_y = ty + 20;
	unsigned int mem_top = bottom_y - 56;

	for(int i = 0; i < MAX_TASKS; i++){
		if(tasks[i].state == TASK_UNUSED) continue;
		if(row_y + 12 >= mem_top) break;

		unsigned int sc = (tasks[i].state <= 4) ? scols[tasks[i].state] : C_DIM;
		dot((int)(tx + 4), (int)(row_y + 5), 3, sc);

		// name
		char nb[14]; int ni = 0;
		while(tasks[i].name[ni] && ni < 12){ nb[ni] = tasks[i].name[ni]; ni++; }
		while(ni < 12) nb[ni++] = ' '; nb[12] = '\0';
		txt(tx + 12, row_y, nb, C_TEXT, C_PANEL_R);

		// state
		const char *sl = (tasks[i].state <= 4) ? snames[tasks[i].state] : "?";
		txt(tx + 12 + 96, row_y, sl, sc, C_PANEL_R);

		row_y += 13;
	}

	// ── MEMORY header ────────────────────────────────────
	hrule(mem_top, x0, rw);
	fb_fill_rect(x0, mem_top + 1, rw, 16, C_BAR);
	txt(tx, mem_top + 5, "MEMORY", C_ACCENT, C_BAR);
	hrule(mem_top + 17, x0, rw);

	unsigned int my = mem_top + 20;
	unsigned long used  = alloc_used();
	unsigned long avail = alloc_free() + used;
	if(avail == 0) avail = 1;

	char mb[40]; int mp = napp(mb, 0, used / 1024);
	mp = sapp(mb, mp, " KB / 948 MB"); mb[mp] = '\0';
	txt(tx, my, mb, C_TEXT, C_PANEL_R);
	my += 12;

	pbar(tx, my, (rw > 24) ? rw - 24 : 10, 8, used, avail, C_GREEN);
	my += 12;

	char fb2[24]; int fp = sapp(fb2, 0, "free blk: ");
	fp = napp(fb2, fp, alloc_free_blocks()); fb2[fp] = '\0';
	txt(tx, my, fb2, C_DIM, C_PANEL_R);
}

static void draw_taskbar(unsigned int W, unsigned int bottom_y){
	fb_fill_rect(0, bottom_y, W, TASKBAR_H, C_BAR);
	hrule(bottom_y, 0, W);

	unsigned int bx = 4;
	for(int i = 0; i < MAX_TASKS; i++){
		if(tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD) continue;
		unsigned int nlen = (unsigned int)k_strlen(tasks[i].name);
		unsigned int bw   = nlen * 8 + 10;
		if(bx + bw + 4 > W - 110) break;
		unsigned int bc = (i == current_task) ? C_TASKBTN_A : C_TASKBTN;
		fb_fill_rect(bx, bottom_y + 3, bw, TASKBAR_H - 6, bc);
		txt(bx + 5, bottom_y + 7, tasks[i].name, C_TEXT, bc);
		bx += bw + 3;
	}

	// right: version
	const char *ver = HUDOS_NAME " v" HUDOS_VERSION;
	unsigned int vw = (unsigned int)(k_strlen(ver)) * 8;
	txt(W - vw - 6, bottom_y + 7, ver, C_DIM, C_BAR);
}

// ── full redraw ───────────────────────────────────────────────────────────────

static void home_draw(void){
	unsigned int W  = fb_width();
	unsigned int FH = fb_height();
	if(W == 0 || FH == 0) return;

	unsigned int bottom_y = FH - TASKBAR_H;
	unsigned int top_y    = TOPBAR_H;
	unsigned int panel_h  = bottom_y - top_y;
	unsigned int split_x  = W / 2;

	draw_topbar(W);
	draw_left(split_x, top_y, panel_h);
	draw_right(split_x, W, top_y, panel_h, bottom_y);
	vrule(split_x, top_y, panel_h);
	draw_taskbar(W, bottom_y);
}

// ── background refresh task ───────────────────────────────────────────────────

static void home_task_fn(void){
	while(home_running){
		home_draw();
		task_sleep_ms(500);
	}
	// on exit, clear to background color
	fb_fill_rect(0, 0, fb_width(), fb_height(), C_BG);
}

// ── public API ────────────────────────────────────────────────────────────────

void home_start(void){
	if(home_running) return;
	if(fb_width() == 0) return;
	home_running = 1;
	// draw immediately (before task scheduling gets a chance to run)
	home_draw();
	home_task_id = task_create_named(home_task_fn, "home-ui");
	if(home_task_id < 0) home_running = 0;
}

void home_stop(void){
	home_running = 0;
	home_task_id = -1;
}

int home_active(void){
	return home_running;
}
