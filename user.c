//Hudson Strauss
#include "user.h"
#include "task.h"
#include "alloc.h"
#include "string.h"
#include "proc.h"

// per-task EL1 entry context, indexed by task ID
// set before the task is scheduled — no race possible
static struct {
    void         (*fn)(void);
    unsigned long  sp;
} user_ctxs[MAX_TASKS];

// EL2 wrapper: reads this task's context then drops to EL1, never returns
static void user_task_wrapper(void){
    task_t *t = task_current();
    void (*fn)(void)  = user_ctxs[t->id].fn;
    unsigned long sp  = user_ctxs[t->id].sp;
    user_enter_el1(fn, sp);
}

int user_task_create(void (*fn)(void), const char *name){
    unsigned char *el1_stack = (unsigned char *)kmalloc_aligned(4096, 4096);
    if(!el1_stack) return -1;
    unsigned long el1_sp = (unsigned long)(el1_stack + 4096);

    int id = task_create_named(user_task_wrapper, name);
    if(id < 0){ kfree(el1_stack); return -1; }

    // Build a full identity-map page table for this process.
    proc_t *pt = proc_create();
    if(!pt){ kfree(el1_stack); task_kill(id); return -1; }

    // Isolate other EL1 tasks: remove their EL1 stacks from this new table,
    // and remove this task's stack from their tables.
    extern task_t tasks[];
    extern int    num_tasks;
    for(int i = 0; i < num_tasks; i++){
        if(tasks[i].el1_stack && i != id){
            // Other task's stack is absent in our table.
            proc_unmap_range(pt,
                (unsigned long)tasks[i].el1_stack, 4096);
            // Our stack is absent in every other table.
            if(tasks[i].proc)
                proc_unmap_range((proc_t*)tasks[i].proc,
                    (unsigned long)el1_stack, 4096);
        }
    }

    // Commit to the task struct (task is READY but not yet running).
    user_ctxs[id].fn = fn;
    user_ctxs[id].sp = el1_sp;
    tasks[id].el1_stack  = el1_stack;
    tasks[id].proc       = pt;
    tasks[id].ttbr0_el1  = proc_ttbr0(pt);
    return id;
}

void user_enter_el1(void (*fn)(void), unsigned long user_sp){
    // HCR_EL2: set RW (bit 31) so EL1 runs AArch64
    unsigned long hcr;
    __asm__ volatile("mrs %0, HCR_EL2" : "=r"(hcr));
    hcr |= (1UL << 31);
    __asm__ volatile("msr HCR_EL2, %0" :: "r"(hcr));

    // SPSR_EL2: EL1h (bits[3:0]=5), mask FIQ/SError/Debug, unmask IRQ
    unsigned long spsr = 5UL | (1UL << 6) | (1UL << 8) | (1UL << 9);
    __asm__ volatile("msr SPSR_EL2, %0" :: "r"(spsr));
    __asm__ volatile("msr ELR_EL2,  %0" :: "r"((unsigned long)fn));
    __asm__ volatile("msr SP_EL1,   %0" :: "r"(user_sp));

    // VBAR_EL1: point at the kernel vector table so EL1 exceptions reach us.
    extern void exception_vector_table(void);
    __asm__ volatile("msr VBAR_EL1, %0" :: "r"((unsigned long)&exception_vector_table));

    // ── EL1 MMU setup ─────────────────────────────────────────────────────────
    task_t *cur = task_current();
    if(cur->ttbr0_el1){
        // MAIR_EL1: attr0 = Normal WB RW-Alloc, attr1 = Device nGnRnE
        unsigned long mair = 0xFFUL | (0x00UL << 8);
        __asm__ volatile("msr MAIR_EL1, %0" :: "r"(mair));

        // TCR_EL1:
        //   T0SZ=32  4 GB VA space (same as EL2)
        //   IRGN0=01 inner WB WA
        //   ORGN0=01 outer WB WA
        //   SH0=11   inner shareable
        //   TG0=00   4 KB granule
        //   EPD1=1   disable TTBR1 walks (upper half unused)
        //   T1SZ=32  (irrelevant when EPD1=1)
        unsigned long tcr = 32UL
            | (1UL << 8)    // IRGN0
            | (1UL << 10)   // ORGN0
            | (3UL << 12)   // SH0
            | (0UL << 14)   // TG0 = 4 KB
            | (32UL << 16)  // T1SZ
            | (1UL << 23);  // EPD1 = disable TTBR1
        __asm__ volatile("msr TCR_EL1, %0" :: "r"(tcr));

        // TTBR0_EL1: process page table (identity map with isolation)
        __asm__ volatile("msr TTBR0_EL1, %0" :: "r"(cur->ttbr0_el1));
        __asm__ volatile("isb");
        __asm__ volatile("tlbi vmalle1\n dsb sy\n isb");

        // SCTLR_EL1: enable MMU (M), data cache (C), instruction cache (I)
        unsigned long sctlr = (1UL << 0) | (1UL << 2) | (1UL << 12);
        __asm__ volatile("msr SCTLR_EL1, %0" :: "r"(sctlr));
        __asm__ volatile("isb");
    } else {
        // Kernel task without a proc — run EL1 without MMU.
        __asm__ volatile("msr SCTLR_EL1, %0" :: "r"(0UL));
    }

    __asm__ volatile("isb");
    __asm__ volatile("eret");
}
