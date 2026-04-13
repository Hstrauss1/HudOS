//Hudson Strauss
#ifndef FB_H
#define FB_H

// initialize framebuffer via mailbox property interface
// returns 0 on success, -1 on failure
int fb_init(unsigned int width, unsigned int height);

// get framebuffer dimensions
unsigned int fb_width(void);
unsigned int fb_height(void);
unsigned int fb_pitch(void);

// pixel operations (32-bit ARGB)
void fb_put_pixel(unsigned int x, unsigned int y, unsigned int color);
void fb_clear(unsigned int color);

// draw a filled rectangle
void fb_fill_rect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int color);

// text rendering (8x8 bitmap font)
void fb_put_char(unsigned int x, unsigned int y, char c, unsigned int fg, unsigned int bg);
void fb_put_string(unsigned int x, unsigned int y, const char *s, unsigned int fg, unsigned int bg);

// scaled text rendering — each font pixel becomes scale×scale screen pixels
void fb_put_char_scaled(unsigned int x, unsigned int y, char c, unsigned int scale, unsigned int fg, unsigned int bg);
void fb_put_string_scaled(unsigned int x, unsigned int y, const char *s, unsigned int scale, unsigned int fg, unsigned int bg);

// console: auto-advancing text cursor with scrolling
void fb_console_init(unsigned int fg, unsigned int bg);
void fb_console_init_region(unsigned int x, unsigned int y,
                             unsigned int cols, unsigned int rows,
                             unsigned int fg, unsigned int bg);
void fb_console_putc(char c);
void fb_console_puts(const char *s);
void fb_console_clear(void);

// suppress / restore console output (used by homescreen)
void fb_console_enable(void);
void fb_console_disable(void);

// horizontal mirror compensation (default 1 for QEMU raspi4b)
void fb_set_mirror(int m);
int  fb_get_mirror(void);

#endif
