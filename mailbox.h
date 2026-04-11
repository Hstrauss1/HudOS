//Hudson Strauss
#ifndef MAILBOX_H
#define MAILBOX_H

// VideoCore mailbox channels
#define MBOX_CH_POWER  0
#define MBOX_CH_FB     1
#define MBOX_CH_VUART  2
#define MBOX_CH_VCHIQ  3
#define MBOX_CH_LEDS   4
#define MBOX_CH_BTNS   5
#define MBOX_CH_TOUCH  6
#define MBOX_CH_COUNT  7
#define MBOX_CH_PROP   8   // property tags (ARM -> VC)

// send a property tag buffer (must be 16-byte aligned) on the given channel
// returns 1 on success, 0 on failure
int mbox_call(unsigned int channel, volatile unsigned int *buf);

#endif
