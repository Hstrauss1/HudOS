//Hudson Strauss
#include "fat32.h"
#include "sd.h"
#include "string.h"
#include "uart.h"

// ── BPB offsets (little-endian reads from a 512-byte sector buffer) ───────────

static unsigned short rd16(const unsigned char *b, int off){
    return (unsigned short)(b[off] | (b[off+1] << 8));
}
static unsigned int rd32(const unsigned char *b, int off){
    return (unsigned int)(b[off] | (b[off+1]<<8) | (b[off+2]<<16) | (b[off+3]<<24));
}

// ── FAT32 state ───────────────────────────────────────────────────────────────

static struct {
    int          mounted;
    unsigned int part_lba;          // partition start LBA
    unsigned int bytes_per_sector;  // almost always 512
    unsigned int sectors_per_cluster;
    unsigned int fat_lba;           // LBA of FAT #0
    unsigned int data_lba;          // LBA of first data cluster
    unsigned int root_cluster;      // first cluster of root directory
} fs;

// ── helpers ───────────────────────────────────────────────────────────────────

// Read one 512-byte sector into buf. Returns 0 on success.
static int read_sector(unsigned int lba, unsigned char *buf){
    return sd_read(lba, buf, 1);
}

// Follow cluster chain: return next cluster after 'c', or 0 on end/error.
static unsigned int next_cluster(unsigned int c){
    unsigned char buf[512];
    unsigned int fat_offset  = c * 4;
    unsigned int fat_sector  = fs.fat_lba + fat_offset / fs.bytes_per_sector;
    unsigned int byte_in_sec = fat_offset % fs.bytes_per_sector;
    if(read_sector(fat_sector, buf)) return 0;
    unsigned int val = rd32(buf, (int)byte_in_sec) & 0x0FFFFFFFu;
    return (val >= 0x0FFFFFF8u) ? 0 : val; // 0 = end of chain
}

// First LBA of a data cluster.
static unsigned int cluster_lba(unsigned int c){
    return fs.data_lba + (c - 2) * fs.sectors_per_cluster;
}

// ── 8.3 name helpers ──────────────────────────────────────────────────────────

static char to_upper(char c){
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

// Convert directory entry 11-byte 8.3 name to a null-terminated string.
// e.g. "FOO     BAR" → "FOO.BAR", "FOO        " → "FOO"
static void entry_to_str(const unsigned char *name11, char *out){
    int i = 0;
    // name part (8 chars)
    int nlen = 0;
    for(int j = 7; j >= 0; j--) if(name11[j] != ' '){ nlen = j + 1; break; }
    for(int j = 0; j < nlen; j++) out[i++] = (char)name11[j];
    // extension (3 chars)
    int elen = 0;
    for(int j = 10; j >= 8; j--) if(name11[j] != ' '){ elen = j - 7; break; }
    if(elen > 0){
        out[i++] = '.';
        for(int j = 8; j < 8 + elen; j++) out[i++] = (char)name11[j];
    }
    out[i] = '\0';
}

// Match a path component (e.g. "foo" or "FOO.BAR") against a raw 8.3 entry.
// Case-insensitive. Returns 1 if match.
static int name_match(const char *comp, const unsigned char *name11){
    // build canonical 8.3 from comp: split at '.', pad with spaces
    char n8[8], e3[3];
    int ni = 0, ei = 0;
    const char *p = comp;
    // name part
    while(*p && *p != '.' && ni < 8) n8[ni++] = to_upper(*p++);
    while(ni < 8) n8[ni++] = ' ';
    if(*p == '.') p++;
    // ext part
    while(*p && ei < 3) e3[ei++] = to_upper(*p++);
    while(ei < 3) e3[ei++] = ' ';

    for(int i = 0; i < 8; i++) if(to_upper(n8[i]) != to_upper(name11[i])) return 0;
    for(int i = 0; i < 3; i++) if(to_upper(e3[i]) != to_upper(name11[8+i])) return 0;
    return 1;
}

// ── directory traversal ───────────────────────────────────────────────────────

#define ATTR_DIR    0x10
#define ATTR_LFN    0x0F    // long file name entry — skip

typedef struct {
    int          found;
    unsigned int cluster;   // first cluster
    unsigned int size;      // file size (0 for dirs)
    int          is_dir;
} dirent_t;

// Search directory starting at 'dir_cluster' for 'name'. Fills result.
static void dir_find(unsigned int dir_cluster, const char *name, dirent_t *res){
    res->found = 0;
    unsigned char buf[512];
    unsigned int c = dir_cluster;

    while(c){
        unsigned int lba = cluster_lba(c);
        for(unsigned int s = 0; s < fs.sectors_per_cluster; s++){
            if(read_sector(lba + s, buf)) return;
            for(int e = 0; e < 512 / 32; e++){
                unsigned char *ent = buf + e * 32;
                if(ent[0] == 0x00) return;          // no more entries
                if(ent[0] == 0xE5) continue;        // deleted
                if(ent[11] == ATTR_LFN) continue;   // LFN, skip
                if(ent[0] == '.' ) continue;        // . and ..
                if(name_match(name, ent)){
                    unsigned int hi = rd16(ent, 20);
                    unsigned int lo = rd16(ent, 26);
                    res->cluster = (hi << 16) | lo;
                    res->size    = rd32(ent, 28);
                    res->is_dir  = (ent[11] & ATTR_DIR) != 0;
                    res->found   = 1;
                    return;
                }
            }
        }
        c = next_cluster(c);
    }
}

// Walk an absolute path to its final component. Returns first cluster + is_dir.
// path must start with '/'. Returns 0 cluster on failure.
static dirent_t path_resolve(const char *path){
    dirent_t res;
    res.found   = 1;
    res.cluster = fs.root_cluster;
    res.is_dir  = 1;
    res.size    = 0;

    const char *p = path;
    if(*p == '/') p++;

    while(*p){
        // extract next component
        char comp[13];
        int ci = 0;
        while(*p && *p != '/' && ci < 12) comp[ci++] = *p++;
        comp[ci] = '\0';
        if(*p == '/') p++;
        if(!comp[0]) continue;

        if(!res.is_dir){ res.found = 0; return res; }
        dirent_t child;
        dir_find(res.cluster, comp, &child);
        if(!child.found){ res.found = 0; return res; }
        res = child;
    }
    return res;
}

// ── mount ─────────────────────────────────────────────────────────────────────

int fat32_mounted(void){ return fs.mounted; }

int fat32_mount(void){
    fs.mounted = 0;
    if(!sd_ready()) return -1;

    unsigned char buf[512];

    // Try LBA 0 as the boot sector first (raw FAT32 image)
    // If it's an MBR, scan partition table for a FAT32 entry
    if(read_sector(0, buf)) return -1;

    unsigned int part_lba = 0;

    // Check MBR signature
    if(buf[510] == 0x55 && buf[511] == 0xAA){
        // Scan up to 4 primary partitions for FAT32 (type 0x0B or 0x0C)
        for(int i = 0; i < 4; i++){
            unsigned char *pe = buf + 446 + i * 16;
            unsigned char type = pe[4];
            if(type == 0x0B || type == 0x0C){
                part_lba = rd32(pe, 8);
                break;
            }
        }
    }

    // Read boot sector of the partition (or LBA 0 for raw)
    if(part_lba && read_sector(part_lba, buf)) return -1;

    // Validate FAT32 signature
    if(buf[510] != 0x55 || buf[511] != 0xAA){
        uart_puts("[fat32] bad boot sector signature\n");
        return -1;
    }
    // Check filesystem type string at offset 82: "FAT32   "
    if(buf[82]!='F'||buf[83]!='A'||buf[84]!='T'||buf[85]!='3'||buf[86]!='2'){
        uart_puts("[fat32] not a FAT32 partition\n");
        return -1;
    }

    fs.part_lba           = part_lba;
    fs.bytes_per_sector   = rd16(buf, 11);
    fs.sectors_per_cluster = buf[13];
    unsigned int reserved  = rd16(buf, 14);
    unsigned int num_fats  = buf[16];
    unsigned int fat_size  = rd32(buf, 36); // FAT32 sectors per FAT
    fs.root_cluster        = rd32(buf, 44);

    fs.fat_lba  = part_lba + reserved;
    fs.data_lba = fs.fat_lba + num_fats * fat_size;

    fs.mounted = 1;

    uart_puts("[fat32] mounted: root_cluster=");
    uart_puthex(fs.root_cluster);
    uart_puts(" spc=");
    uart_puthex(fs.sectors_per_cluster);
    uart_puts("\n");
    return 0;
}

// ── readdir ───────────────────────────────────────────────────────────────────

int fat32_readdir(const char *path,
                  void (*cb)(const char *name, int is_dir, unsigned int size)){
    if(!fs.mounted) return -1;

    dirent_t dir = path_resolve(path);
    if(!dir.found || !dir.is_dir) return -1;

    unsigned char buf[512];
    unsigned int c = dir.cluster;

    while(c){
        unsigned int lba = cluster_lba(c);
        for(unsigned int s = 0; s < fs.sectors_per_cluster; s++){
            if(read_sector(lba + s, buf)) return -1;
            for(int e = 0; e < 512 / 32; e++){
                unsigned char *ent = buf + e * 32;
                if(ent[0] == 0x00) return 0;
                if(ent[0] == 0xE5) continue;
                if(ent[11] == ATTR_LFN) continue;
                if(ent[0] == '.') continue;
                char name[13];
                entry_to_str(ent, name);
                int  is_dir = (ent[11] & ATTR_DIR) != 0;
                unsigned int sz = rd32(ent, 28);
                cb(name, is_dir, sz);
            }
        }
        c = next_cluster(c);
    }
    return 0;
}

// ── load file ─────────────────────────────────────────────────────────────────

int fat32_load(const char *path, unsigned char *buf, unsigned int maxlen){
    if(!fs.mounted) return -1;

    dirent_t f = path_resolve(path);
    if(!f.found || f.is_dir) return -1;

    unsigned int to_read  = f.size < maxlen ? f.size : maxlen;
    unsigned int done     = 0;
    unsigned int c        = f.cluster;
    unsigned int cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;

    while(c && done < to_read){
        unsigned int lba = cluster_lba(c);
        for(unsigned int s = 0; s < fs.sectors_per_cluster && done < to_read; s++){
            unsigned char sec[512];
            if(read_sector(lba + s, sec)) return -1;
            unsigned int copy = 512;
            if(done + copy > to_read) copy = to_read - done;
            memcpy(buf + done, sec, copy);
            done += copy;
        }
        c = next_cluster(c);
        (void)cluster_bytes;
    }
    return (int)done;
}
