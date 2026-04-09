//Hudson Strauss
// AArch64 EL2 MMU setup — identity-mapped, 4KB granule, 2MB block descriptors
//
// Memory map:
//   0x00000000 - 0x3BFFFFFF  Normal memory (RAM, ~960MB)
//   0x3C000000 - 0xFFFFFFFF  Device-nGnRnE (MMIO peripherals)
//
// Uses MAIR index 0 = Normal WB cacheable, index 1 = Device-nGnRnE
// Level 1 table with 2MB blocks (512 entries covers 1GB)

#include "mmu.h"
#include "alloc.h"
#include "string.h"

// page table entry bits
#define PT_VALID        (1UL << 0)
#define PT_BLOCK        (0UL << 1)  // block descriptor at L1
#define PT_AF           (1UL << 10) // access flag
#define PT_SH_INNER     (3UL << 8)  // inner shareable
#define PT_SH_OUTER     (2UL << 8)  // outer shareable
#define PT_AP_RW        (0UL << 6)  // EL2 read/write

// MAIR attribute indices (set in mmu_init)
#define MAIR_NORMAL_IDX 0
#define MAIR_DEVICE_IDX 1
#define PT_ATTR_NORMAL  (MAIR_NORMAL_IDX << 2)
#define PT_ATTR_DEVICE  (MAIR_DEVICE_IDX << 2)

#define BLOCK_SIZE      (2UL * 1024 * 1024) // 2MB
#define RAM_END         0x3C000000UL         // everything below is normal RAM
#define MMIO_END        0x100000000UL        // map up to 4GB

// 512 entries * 2MB = 1GB per L1 table
// we need 4 L1 tables for 4GB, linked from a single L0 table
#define L1_ENTRIES      512

static unsigned long *l0_table;
static unsigned long *l1_tables; // 4 contiguous L1 tables

void mmu_init(void){
	// allocate page tables — must be 4KB aligned
	l0_table = (unsigned long *)kmalloc_aligned(4096, 4096);
	l1_tables = (unsigned long *)kmalloc_aligned(4 * 4096, 4096);

	memset(l0_table, 0, 4096);
	memset(l1_tables, 0, 4 * 4096);

	// L0: 4 entries pointing to L1 tables (table descriptor)
	for(int i = 0; i < 4; i++){
		unsigned long l1_addr = (unsigned long)&l1_tables[i * L1_ENTRIES];
		l0_table[i] = l1_addr | PT_VALID | (1UL << 1); // table descriptor
	}

	// L1: fill with 2MB block descriptors
	for(int i = 0; i < 4; i++){
		for(int j = 0; j < L1_ENTRIES; j++){
			unsigned long phys = (unsigned long)i * (512UL * BLOCK_SIZE) + (unsigned long)j * BLOCK_SIZE;
			unsigned long entry = phys | PT_VALID | PT_BLOCK | PT_AF | PT_AP_RW;

			if(phys < RAM_END){
				// normal cacheable memory, inner shareable
				entry |= PT_ATTR_NORMAL | PT_SH_INNER;
			} else {
				// device memory, outer shareable
				entry |= PT_ATTR_DEVICE | PT_SH_OUTER;
			}

			l1_tables[i * L1_ENTRIES + j] = entry;
		}
	}

	// MAIR_EL2:
	//   attr0 = 0xFF (Normal, Write-Back, Read/Write Allocate)
	//   attr1 = 0x00 (Device-nGnRnE)
	unsigned long mair = 0xFF | (0x00UL << 8);
	__asm__ volatile("msr MAIR_EL2, %0" :: "r"(mair));

	// TCR_EL2:
	//   T0SZ = 32 (32-bit address space, 4GB)
	//   IRGN0 = 01 (inner WB RA WA cacheable)
	//   ORGN0 = 01 (outer WB RA WA cacheable)
	//   SH0   = 11 (inner shareable)
	//   TG0   = 00 (4KB granule)
	//   PS    = 000 (32-bit physical, 4GB)
	unsigned long tcr = 32UL         // T0SZ = 32
		| (1UL << 8)                 // IRGN0
		| (1UL << 10)                // ORGN0
		| (3UL << 12)                // SH0 = inner shareable
		| (0UL << 14)                // TG0 = 4KB
		| (0UL << 16);              // PS = 32-bit
	__asm__ volatile("msr TCR_EL2, %0" :: "r"(tcr));

	// TTBR0_EL2: base of L0 table
	__asm__ volatile("msr TTBR0_EL2, %0" :: "r"((unsigned long)l0_table));

	// barriers before enabling
	__asm__ volatile("isb");
	__asm__ volatile("tlbi alle2");
	__asm__ volatile("dsb sy");
	__asm__ volatile("isb");

	// SCTLR_EL2: enable MMU (bit 0), enable data cache (bit 2), enable instruction cache (bit 12)
	unsigned long sctlr;
	__asm__ volatile("mrs %0, SCTLR_EL2" : "=r"(sctlr));
	sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);
	__asm__ volatile("msr SCTLR_EL2, %0" :: "r"(sctlr));
	__asm__ volatile("isb");
}
