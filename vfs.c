//Hudson Strauss
#include "vfs.h"
#include "alloc.h"
#include "string.h"

static vfs_inode_t inodes[VFS_MAX_INODES];
static vfs_fd_t    fds[VFS_MAX_FDS];

// ── helpers ───────────────────────────────────────────────────────────────────

static int alloc_inode(void){
    for(int i = 1; i < VFS_MAX_INODES; i++) // 0 reserved for root
        if(!inodes[i].used) return i;
    return -1;
}

static int alloc_fd(void){
    for(int i = 0; i < VFS_MAX_FDS; i++)
        if(fds[i].inode == -1) return i;
    return -1;
}

// copy at most n-1 chars from src into dst, always null-terminate
static void vfs_strncpy(char *dst, const char *src, int n){
    int i = 0;
    while(src[i] && i < n - 1){ dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// split an absolute path into parent path and final name component
// e.g. "/foo/bar" -> parent="/foo", name="bar"
//      "/foo"     -> parent="/",    name="foo"
//      "foo"      -> parent="/",    name="foo"  (treated as /foo)
// returns 1 on success, 0 if path is empty or name would be empty
static int split_path(const char *path, char *parent, char *name){
    int len = k_strlen(path);
    if(len == 0) return 0;

    // find last '/'
    int slash = -1;
    for(int i = 0; i < len; i++)
        if(path[i] == '/') slash = i;

    if(slash < 0){
        // no slash — relative, treat parent as root
        parent[0] = '/'; parent[1] = '\0';
        vfs_strncpy(name, path, VFS_MAX_NAME);
    } else if(slash == 0){
        // "/name"
        parent[0] = '/'; parent[1] = '\0';
        vfs_strncpy(name, path + 1, VFS_MAX_NAME);
    } else {
        // "/foo/bar" — parent is everything before last slash
        int i = 0;
        for(; i < slash && i < VFS_MAX_NAME - 1; i++) parent[i] = path[i];
        parent[i] = '\0';
        vfs_strncpy(name, path + slash + 1, VFS_MAX_NAME);
    }
    return name[0] != '\0';
}

// ── init ──────────────────────────────────────────────────────────────────────

void vfs_init(void){
    memset(inodes, 0, sizeof(inodes));
    for(int i = 0; i < VFS_MAX_FDS; i++) fds[i].inode = -1;

    // inode 0 is always the root directory
    inodes[0].used   = 1;
    inodes[0].type   = VFS_DIR;
    inodes[0].parent = -1;
    inodes[0].size   = 0;
    inodes[0].data   = 0;
    inodes[0].name[0] = '/';
    inodes[0].name[1] = '\0';
}

// ── path resolution ───────────────────────────────────────────────────────────

int vfs_resolve(const char *path){
    if(!path || path[0] == '\0') return -1;
    if(path[0] == '/' && path[1] == '\0') return 0; // root

    int cur = 0;
    const char *p = path;
    if(*p == '/') p++;

    while(*p){
        // extract next component
        char comp[VFS_MAX_NAME];
        int i = 0;
        while(*p && *p != '/' && i < VFS_MAX_NAME - 1)
            comp[i++] = *p++;
        comp[i] = '\0';
        if(*p == '/') p++;
        if(comp[0] == '\0') continue; // skip double slashes

        // find a child of cur named comp
        int found = -1;
        for(int j = 1; j < VFS_MAX_INODES; j++){
            if(inodes[j].used && inodes[j].parent == cur && str_eq(inodes[j].name, comp)){
                found = j;
                break;
            }
        }
        if(found < 0) return -1;
        cur = found;
    }
    return cur;
}

// ── mkdir ─────────────────────────────────────────────────────────────────────

int vfs_mkdir(const char *path){
    char ppath[VFS_MAX_NAME], name[VFS_MAX_NAME];
    if(!split_path(path, ppath, name)) return -1;

    int par = vfs_resolve(ppath);
    if(par < 0 || inodes[par].type != VFS_DIR) return -1;

    // already exists?
    for(int i = 1; i < VFS_MAX_INODES; i++){
        if(inodes[i].used && inodes[i].parent == par && str_eq(inodes[i].name, name))
            return -1;
    }

    int idx = alloc_inode();
    if(idx < 0) return -1;

    inodes[idx].used   = 1;
    inodes[idx].type   = VFS_DIR;
    inodes[idx].parent = par;
    inodes[idx].size   = 0;
    inodes[idx].data   = 0;
    vfs_strncpy(inodes[idx].name, name, VFS_MAX_NAME);
    return idx;
}

// ── open / close ──────────────────────────────────────────────────────────────

int vfs_open(const char *path, int flags){
    int idx = vfs_resolve(path);

    if(idx < 0){
        if(!(flags & O_CREAT)) return -1;

        char ppath[VFS_MAX_NAME], name[VFS_MAX_NAME];
        if(!split_path(path, ppath, name)) return -1;

        int par = vfs_resolve(ppath);
        if(par < 0 || inodes[par].type != VFS_DIR) return -1;

        idx = alloc_inode();
        if(idx < 0) return -1;

        inodes[idx].data = (unsigned char *)kmalloc(VFS_MAX_FSIZE);
        if(!inodes[idx].data){ inodes[idx].used = 0; return -1; }
        memset(inodes[idx].data, 0, VFS_MAX_FSIZE);

        inodes[idx].used   = 1;
        inodes[idx].type   = VFS_FILE;
        inodes[idx].parent = par;
        inodes[idx].size   = 0;
        vfs_strncpy(inodes[idx].name, name, VFS_MAX_NAME);
    } else {
        if(inodes[idx].type != VFS_FILE) return -1;
        if(flags & O_TRUNC){
            if(inodes[idx].data) memset(inodes[idx].data, 0, VFS_MAX_FSIZE);
            inodes[idx].size = 0;
        }
    }

    int fd = alloc_fd();
    if(fd < 0) return -1;

    fds[fd].inode = idx;
    fds[fd].flags = flags;
    fds[fd].pos   = (flags & O_APPEND) ? inodes[idx].size : 0;
    return fd;
}

void vfs_close(int fd){
    if(fd >= 0 && fd < VFS_MAX_FDS) fds[fd].inode = -1;
}

// ── read / write / seek ───────────────────────────────────────────────────────

int vfs_read(int fd, void *buf, int len){
    if(fd < 0 || fd >= VFS_MAX_FDS || fds[fd].inode < 0) return -1;
    vfs_inode_t *in = &inodes[fds[fd].inode];
    if(in->type != VFS_FILE || !in->data) return -1;

    int avail = in->size - fds[fd].pos;
    if(avail <= 0) return 0;
    if(len > avail) len = avail;
    memcpy(buf, in->data + fds[fd].pos, len);
    fds[fd].pos += len;
    return len;
}

int vfs_write(int fd, const void *buf, int len){
    if(fd < 0 || fd >= VFS_MAX_FDS || fds[fd].inode < 0) return -1;
    vfs_inode_t *in = &inodes[fds[fd].inode];
    if(in->type != VFS_FILE || !in->data) return -1;

    if(fds[fd].flags & O_APPEND) fds[fd].pos = in->size;

    int space = VFS_MAX_FSIZE - fds[fd].pos;
    if(space <= 0) return -1;
    if(len > space) len = space;
    memcpy(in->data + fds[fd].pos, buf, len);
    fds[fd].pos += len;
    if(fds[fd].pos > in->size) in->size = fds[fd].pos;
    return len;
}

int vfs_seek(int fd, int offset, int whence){
    if(fd < 0 || fd >= VFS_MAX_FDS || fds[fd].inode < 0) return -1;
    vfs_inode_t *in = &inodes[fds[fd].inode];

    int np;
    if(whence == SEEK_SET)      np = offset;
    else if(whence == SEEK_CUR) np = fds[fd].pos + offset;
    else if(whence == SEEK_END) np = in->size + offset;
    else return -1;

    if(np < 0 || np > in->size) return -1;
    fds[fd].pos = np;
    return np;
}

// ── unlink ────────────────────────────────────────────────────────────────────

int vfs_unlink(const char *path){
    int idx = vfs_resolve(path);
    if(idx <= 0) return -1; // not found or root

    if(inodes[idx].type == VFS_DIR){
        // refuse if non-empty
        for(int i = 1; i < VFS_MAX_INODES; i++)
            if(inodes[i].used && inodes[i].parent == idx) return -1;
    }

    // close any open fds pointing here
    for(int i = 0; i < VFS_MAX_FDS; i++)
        if(fds[i].inode == idx) fds[i].inode = -1;

    if(inodes[idx].data){ kfree(inodes[idx].data); }
    memset(&inodes[idx], 0, sizeof(vfs_inode_t));
    return 0;
}

// ── inode inspection ──────────────────────────────────────────────────────────

int vfs_inode_used(int i){
    return i >= 0 && i < VFS_MAX_INODES && inodes[i].used;
}
int vfs_inode_type(int i){
    if(!vfs_inode_used(i)) return -1;
    return inodes[i].type;
}
int vfs_inode_size(int i){
    if(!vfs_inode_used(i)) return -1;
    return inodes[i].size;
}
int vfs_inode_parent(int i){
    if(!vfs_inode_used(i)) return -1;
    return inodes[i].parent;
}
const char *vfs_inode_name(int i){
    if(!vfs_inode_used(i)) return "";
    return inodes[i].name;
}
int vfs_max_inodes(void){ return VFS_MAX_INODES; }
