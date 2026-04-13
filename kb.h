//Hudson Strauss
#ifndef KB_H
#define KB_H

// USB HID keyboard driver backed by the BCM2711 DWC2 USB2 OTG host controller.
//
// Usage:
//   kb_init()   — initialize DWC2, enumerate attached USB keyboard
//                 Returns 0 on success, -1 if no keyboard found.
//                 Safe to call again to re-enumerate.
//   kb_poll()   — call periodically (~10ms); processes any new HID reports
//   kb_ready()  — returns 1 if a decoded ASCII character is waiting
//   kb_getc()   — pop and return next ASCII char, or 0 if none

int  kb_init(void);
void kb_poll(void);
int  kb_ready(void);
char kb_getc(void);

#endif
