//Hudson Strauss
#include "mailbox.h"
#include "mmio.h"

int mbox_call(unsigned int channel, volatile unsigned int *buf){
	// address must be 16-byte aligned, lower 4 bits used for channel
	unsigned int addr = (unsigned int)((unsigned long)buf & 0xFFFFFFF0);
	unsigned int msg = addr | (channel & 0xF);

	// wait until mailbox is not full
	while(MBOX_STATUS & MBOX_FULL)
		;

	// write message
	MBOX_WRITE = msg;

	// wait for reply
	while(1){
		while(MBOX_STATUS & MBOX_EMPTY)
			;
		unsigned int reply = MBOX_READ;
		// check if it's our channel
		if((reply & 0xF) == channel){
			// check response code (word 1 of buffer)
			return buf[1] == 0x80000000; // success
		}
	}
}
