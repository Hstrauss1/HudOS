//Hudson Strauss
#ifndef SD_H
#define SD_H

#define SD_BLOCK_SIZE 512

// Initialize the SD card via the BCM2711 Arasan EMMC controller.
// Must be called before any read/write. Returns 0 on success, -1 on failure.
int sd_init(void);

// Read 'count' 512-byte blocks from LBA 'lba' into buf.
// buf must be at least count * 512 bytes and 4-byte aligned.
// Returns 0 on success, -1 on error.
int sd_read(unsigned int lba, unsigned char *buf, unsigned int count);

// Write 'count' 512-byte blocks from buf to LBA 'lba'.
// Returns 0 on success, -1 on error.
int sd_write(unsigned int lba, const unsigned char *buf, unsigned int count);

// Returns 1 if sd_init has succeeded, 0 otherwise.
int sd_ready(void);

#endif
