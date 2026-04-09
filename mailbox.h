//Hudson Strauss
#ifndef MAILBOX_H
#define MAILBOX_H

// send a property tag buffer (must be 16-byte aligned) on the given channel
// returns 1 on success, 0 on failure
int mbox_call(unsigned int channel, volatile unsigned int *buf);

#endif
