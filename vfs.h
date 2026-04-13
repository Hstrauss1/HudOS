//Hudson Strauss
#ifndef VFS_H
#define VFS_H

// inode types
#define VFS_FILE  0
#define VFS_DIR   1

// open flags
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   4
#define O_APPEND  8
#define O_TRUNC   16

// seek whence
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

// limits
#define VFS_MAX_INODES  64
#define VFS_MAX_NAME    32
#define VFS_MAX_FDS     16
#define VFS_MAX_FSIZE   16384   // 16 KB per file

typedef struct {
    char          name[VFS_MAX_NAME];
    int           type;    // VFS_FILE or VFS_DIR
    int           parent;  // inode index of parent dir; -1 for root
    int           size;    // bytes stored (files only)
    unsigned char *data;   // heap-allocated (files only)
    int           used;
} vfs_inode_t;

typedef struct {
    int inode;  // -1 = closed
    int pos;
    int flags;
} vfs_fd_t;

void vfs_init(void);

// path ops — all paths must be absolute ("/foo/bar")
int  vfs_resolve(const char *path);   // returns inode index or -1
int  vfs_mkdir(const char *path);     // returns inode index or -1
int  vfs_unlink(const char *path);    // returns 0 or -1

// file ops — return fd >= 0 or -1 on error
int  vfs_open(const char *path, int flags);
void vfs_close(int fd);
int  vfs_read(int fd, void *buf, int len);
int  vfs_write(int fd, const void *buf, int len);
int  vfs_seek(int fd, int offset, int whence);

// inode inspection (for ls, stat, etc.)
int         vfs_inode_used(int inode);
int         vfs_inode_type(int inode);
int         vfs_inode_size(int inode);
int         vfs_inode_parent(int inode);
const char *vfs_inode_name(int inode);
int         vfs_max_inodes(void);

#endif
