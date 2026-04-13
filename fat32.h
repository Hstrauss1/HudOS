//Hudson Strauss
#ifndef FAT32_H
#define FAT32_H

// Read-only FAT32 driver backed by the SD card block driver.
//
// Call fat32_mount() once after sd_init() succeeds.
// All paths are Unix-style absolute: "/bin/shell", "/etc/config".
// Only 8.3 filenames are matched (case-insensitive); LFN entries are skipped.

// Mount the first FAT32 partition found on the SD card (or a raw FAT32 image).
// Returns 0 on success, -1 on failure.
int fat32_mount(void);

// Returns 1 if fat32_mount has succeeded.
int fat32_mounted(void);

// List a directory. Calls cb(name, is_dir, file_size) for each valid entry.
// Returns 0 on success, -1 if path not found or not a directory.
int fat32_readdir(const char *path,
                  void (*cb)(const char *name, int is_dir, unsigned int size));

// Read a file entirely into buf (up to maxlen bytes).
// Returns bytes read, or -1 on error.
int fat32_load(const char *path, unsigned char *buf, unsigned int maxlen);

#endif
