//Hudson Strauss
#include "elf.h"
#include "vfs.h"
#include "alloc.h"
#include "string.h"
#include "user.h"
#include "uart.h"

// ── ELF64 structures ──────────────────────────────────────────────────────────

#define ELF_MAGIC    0x464C457FU    // "\x7fELF" little-endian
#define ELFCLASS64   2
#define ELFDATA2LSB  1              // little-endian
#define ET_EXEC      2              // executable
#define EM_AARCH64   0x00B7        // AArch64 machine type
#define PT_LOAD      1              // loadable segment

typedef struct {
    unsigned int   e_magic;         // 0x7F 'E' 'L' 'F'
    unsigned char  e_class;         // 2 = ELF64
    unsigned char  e_data;          // 1 = LE
    unsigned char  e_version_id;    // 1
    unsigned char  e_os_abi;
    unsigned char  e_pad[8];
    unsigned short e_type;          // ET_EXEC
    unsigned short e_machine;       // EM_AARCH64
    unsigned int   e_version;
    unsigned long  e_entry;         // entry point virtual address
    unsigned long  e_phoff;         // program header table offset
    unsigned long  e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;     // size of one program header entry
    unsigned short e_phnum;         // number of program headers
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} __attribute__((packed)) elf64_hdr_t;

typedef struct {
    unsigned int   p_type;          // PT_LOAD etc.
    unsigned int   p_flags;         // PF_R/PF_W/PF_X
    unsigned long  p_offset;        // offset in file
    unsigned long  p_vaddr;         // virtual address to load to
    unsigned long  p_paddr;         // physical address (same as vaddr here)
    unsigned long  p_filesz;        // bytes in file
    unsigned long  p_memsz;         // bytes in memory (>= filesz, remainder zeroed)
    unsigned long  p_align;
} __attribute__((packed)) elf64_phdr_t;

// ── shared loader core ────────────────────────────────────────────────────────

// Validate, load PT_LOAD segments, and spawn a user task from an ELF64 buffer.
// Returns task ID on success, -1 on error.
static int elf_load_buf(const unsigned char *buf, unsigned int size,
                        const char *task_name){
    if(size < sizeof(elf64_hdr_t)){
        uart_puts("[elf] file too small\n");
        return -1;
    }

    const elf64_hdr_t *hdr = (const elf64_hdr_t *)buf;

    if(hdr->e_magic   != ELF_MAGIC ||
       hdr->e_class   != ELFCLASS64 ||
       hdr->e_data    != ELFDATA2LSB ||
       hdr->e_type    != ET_EXEC ||
       hdr->e_machine != EM_AARCH64){
        uart_puts("[elf] bad header (not AArch64 ELF64 executable)\n");
        return -1;
    }

    if(hdr->e_phnum == 0 || hdr->e_phentsize < sizeof(elf64_phdr_t)){
        uart_puts("[elf] no program headers\n");
        return -1;
    }

    for(int i = 0; i < hdr->e_phnum; i++){
        unsigned long phoff = hdr->e_phoff + (unsigned long)i * hdr->e_phentsize;
        if(phoff + sizeof(elf64_phdr_t) > (unsigned long)size) break;

        const elf64_phdr_t *ph = (const elf64_phdr_t *)(buf + phoff);
        if(ph->p_type != PT_LOAD) continue;
        if(ph->p_memsz == 0) continue;

        void *dst = (void *)ph->p_vaddr;

        unsigned long copy = ph->p_filesz;
        if(ph->p_offset + copy > (unsigned long)size)
            copy = (unsigned long)size - ph->p_offset;
        if(copy > 0)
            memcpy(dst, buf + ph->p_offset, copy);

        if(ph->p_memsz > ph->p_filesz)
            memset((unsigned char *)dst + ph->p_filesz, 0,
                   ph->p_memsz - ph->p_filesz);
    }

    unsigned long entry = hdr->e_entry;
    if(entry == 0){
        uart_puts("[elf] zero entry point\n");
        return -1;
    }

    void (*fn)(void) = (void (*)(void))entry;
    int tid = user_task_create(fn, task_name);
    if(tid < 0){
        uart_puts("[elf] failed to create task\n");
        return -1;
    }
    return tid;
}

// ── public API ────────────────────────────────────────────────────────────────

int elf_exec_buf(const unsigned char *buf, unsigned int size,
                 const char *task_name){
    return elf_load_buf(buf, size, task_name);
}

int elf_exec(const char *path, const char *task_name){
    // ── 1. read the file into a temp buffer ──────────────────────────────────
    int fd = vfs_open(path, O_RDONLY);
    if(fd < 0){
        uart_puts("[elf] not found: ");
        uart_puts(path);
        uart_puts("\n");
        return -1;
    }

    int filesz = vfs_seek(fd, 0, SEEK_END);
    vfs_seek(fd, 0, SEEK_SET);

    if(filesz <= 0 || filesz > VFS_MAX_FSIZE){
        uart_puts("[elf] file too large or empty\n");
        vfs_close(fd);
        return -1;
    }

    unsigned char *buf = (unsigned char *)kmalloc(filesz);
    if(!buf){
        uart_puts("[elf] out of memory\n");
        vfs_close(fd);
        return -1;
    }
    vfs_read(fd, buf, filesz);
    vfs_close(fd);

    // ── 2. load via shared core ──────────────────────────────────────────────
    int tid = elf_load_buf(buf, (unsigned int)filesz, task_name);
    kfree(buf);
    return tid;
}
