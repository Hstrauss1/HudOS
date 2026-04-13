//Hudson Strauss
#ifndef PROC_H
#define PROC_H

// Per-process EL1 page table.
//
// Structure mirrors the kernel's EL2 identity map:
//   l1[4]    root table  (4 entries × 1 GB = 4 GB; ARM level-1 with T0SZ=32)
//   l2[4]    four L2 tables (512 entries × 2 MB each)
//   l3[N]    optional L3 tables (4 KB pages) for page-level isolation
//
// proc_create()  builds a full identity map.
// proc_unmap_range() breaks the relevant 2 MB block(s) into 4 KB pages,
//                    then marks each target page absent (entry = 0).
// proc_free()    frees all page table memory.
// proc_ttbr0()   returns the physical address to load into TTBR0_EL1.

#define PROC_MAX_L3  16   // max 2MB blocks broken to 4KB per process

typedef struct {
    unsigned long *l1;             // root table  (page-aligned, 4 valid entries)
    unsigned long *l2[4];          // L2 tables   (4096 bytes each)
    unsigned long *l3[PROC_MAX_L3];// L3 tables created by break-down
    int            l3_count;
} proc_t;

proc_t       *proc_create(void);
void          proc_unmap_range(proc_t *p, unsigned long va, unsigned long size);
void          proc_free(proc_t *p);
unsigned long proc_ttbr0(proc_t *p);

#endif
