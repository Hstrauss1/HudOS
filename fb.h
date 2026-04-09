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

// console: auto-advancing text cursor with scrolling
void fb_console_init(unsigned int fg, unsigned int bg);
void fb_console_putc(char c);
void fb_console_puts(const char *s);
void fb_console_clear(void);

#endif
