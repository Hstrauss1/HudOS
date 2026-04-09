//Hudson Strauss
#ifndef RAMFS_H
#define RAMFS_H

#define RAMFS_MAX_FILES   32
#define RAMFS_MAX_NAME    32
#define RAMFS_MAX_SIZE    4096

typedef struct {
	char name[RAMFS_MAX_NAME];
	unsigned char *data;
	int size;
	int used;   // 1 = slot in use
} ramfs_file_t;

void ramfs_init(void);

// returns file index or -1
int ramfs_create(const char *name);
int ramfs_find(const char *name);

// read/write — returns bytes transferred
int ramfs_write(int fd, const void *buf, int len);
int ramfs_read(int fd, void *buf, int len);

// info
int ramfs_size(int fd);
const char *ramfs_name(int fd);
int ramfs_count(void);

#endif
