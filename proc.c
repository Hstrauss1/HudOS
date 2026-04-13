//Hudson Strauss
// Per-process EL1 page table management.
// Replicates the kernel's identity map structure (2 MB blocks), then allows
// individual 4 KB pages to be excluded (made absent) for inter-process isolation.

#include "proc.h"
#include "alloc.h"
#include "string.h"

// ── memory map constants (must match mmu.c) ───────────────────────────────────
#define BLOCK_2MB   (2UL * 1024 * 1024)
#define RAM_END     0x3C000000UL   // below this = Normal; above = Device

// ── page table descriptor bit patterns ───────────────────────────────────────

// L1 TABLE descriptor (points to L2 table)
//   bits[1:0] = 11
#define DESC_TABLE(pa)  ((pa) | 3UL)

// L2 BLOCK descriptor (2 MB, Normal WB inner-shareable, EL1 R/W, AF=1)
#define DESC_BLOCK_NORMAL(pa) \
    ((pa) | 1UL          /* block */     \
           | (0UL << 2)  /* AttrIndx=0 Normal WB */ \
           | (0UL << 6)  /* AP=00  EL1 R/W, EL0 none */ \
           | (3UL << 8)  /* SH=11 inner shareable */ \
           | (1UL << 10) /* AF=1 */)

// L2 BLOCK descriptor (2 MB, Device nGnRnE outer-shareable)
#define DESC_BLOCK_DEVICE(pa) \
    ((pa) | 1UL          \
           | (1UL << 2)  /* AttrIndx=1 Device */ \
           | (0UL << 6)  \
           | (2UL << 8)  /* SH=10 outer shareable */ \
           | (1UL << 10))

// L3 PAGE descriptor (4 KB, Normal WB inner-shareable, EL1 R/W, AF=1)
#define DESC_PAGE_NORMAL(pa) \
    ((pa) | 3UL          /* page */ \
           | (0UL << 2)  \
           | (0UL << 6)  \
           | (3UL << 8)  \
           | (1UL << 10))

// L3 PAGE descriptor (4 KB, Device)
#define DESC_PAGE_DEVICE(pa) \
    ((pa) | 3UL          \
           | (1UL << 2)  \
           | (0UL << 6)  \
           | (2UL << 8)  \
           | (1UL << 10))

// ── helpers ───────────────────────────────────────────────────────────────────

// Return or create the L3 table for the 2 MB block containing 'va'.
// Replaces the L2 block descriptor with a TABLE descriptor → new L3 table.
static unsigned long *get_or_create_l3(proc_t *p, unsigned long va){
    int l1i = (int)((va >> 30) & 3);       // which 1 GB region
    int l2i = (int)((va >> 21) & 0x1FFu);  // which 2 MB within that GB

    unsigned long entry = p->l2[l1i][l2i];

    // Already a table descriptor (bits[1:0] == 11 for TABLE at L2)?
    if((entry & 3UL) == 3UL){
        return (unsigned long *)(entry & 0x0000FFFFFFFFF000UL);
    }

    // Still a block descriptor — break it down to 4 KB pages.
    if(p->l3_count >= PROC_MAX_L3) return 0;

    unsigned long block_pa  = entry & ~(BLOCK_2MB - 1UL);
    int           is_device = ((entry >> 2) & 7UL) != 0;  // AttrIndx != 0

    unsigned long *l3 = (unsigned long *)kmalloc_aligned(4096, 4096);
    if(!l3) return 0;
    p->l3[p->l3_count++] = l3;

    // Fill 512 × 4 KB page descriptors covering the same physical 2 MB range.
    for(int i = 0; i < 512; i++){
        unsigned long pa = block_pa + (unsigned long)i * 4096;
        l3[i] = is_device ? DESC_PAGE_DEVICE(pa) : DESC_PAGE_NORMAL(pa);
    }

    // Replace the L2 block with a TABLE descriptor pointing to the new L3.
    p->l2[l1i][l2i] = DESC_TABLE((unsigned long)l3);

    // Ensure page-table walker sees the new L3 before we issue TLB ops.
    __asm__ volatile("dsb ishst");

    return l3;
}

// ── public API ────────────────────────────────────────────────────────────────

proc_t *proc_create(void){
    proc_t *p = (proc_t *)kmalloc_aligned(sizeof(proc_t), 16);
    if(!p) return 0;
    memset(p, 0, sizeof(proc_t));

    // Root table: 4 entries, page-aligned.
    p->l1 = (unsigned long *)kmalloc_aligned(4096, 4096);
    if(!p->l1) goto fail;
    memset(p->l1, 0, 4096);

    // Four L2 tables (one per GB of the 4 GB identity space).
    for(int i = 0; i < 4; i++){
        p->l2[i] = (unsigned long *)kmalloc_aligned(4096, 4096);
        if(!p->l2[i]) goto fail;

        for(int j = 0; j < 512; j++){
            unsigned long phys = (unsigned long)i * (512UL * BLOCK_2MB)
                               + (unsigned long)j * BLOCK_2MB;
            p->l2[i][j] = (phys < RAM_END)
                         ? DESC_BLOCK_NORMAL(phys)
                         : DESC_BLOCK_DEVICE(phys);
        }

        // Wire root entry → L2 table.
        p->l1[i] = DESC_TABLE((unsigned long)p->l2[i]);
    }

    return p;

fail:
    proc_free(p);
    return 0;
}

void proc_unmap_range(proc_t *p, unsigned long va, unsigned long size){
    unsigned long page  = va & ~0xFFFUL;           // page-align start
    unsigned long end   = (va + size + 0xFFFUL) & ~0xFFFUL;  // page-align end

    while(page < end){
        unsigned long *l3 = get_or_create_l3(p, page);
        if(l3){
            int l3i = (int)((page >> 12) & 0x1FFu);
            l3[l3i] = 0;  // mark absent

            // Point-of-unification TLB invalidate for EL1.
            __asm__ volatile("dsb ishst");
            __asm__ volatile("tlbi vae1, %0" :: "r"(page >> 12));
            __asm__ volatile("dsb ish\n isb");
        }
        page += 4096;
    }
}

void proc_free(proc_t *p){
    if(!p) return;
    for(int i = 0; i < p->l3_count; i++)
        if(p->l3[i]) kfree(p->l3[i]);
    for(int i = 0; i < 4; i++)
        if(p->l2[i]) kfree(p->l2[i]);
    if(p->l1) kfree(p->l1);
    kfree(p);
}

unsigned long proc_ttbr0(proc_t *p){
    // Identity map: VA == PA, so the pointer is also the physical address.
    return (unsigned long)p->l1;
}
