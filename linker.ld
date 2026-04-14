ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    . = 0x04000000;
    __heap_start = .;
    __heap_end = 0x3B400000;  /* ~948MB, below MMIO at 0xFC000000 */
}
