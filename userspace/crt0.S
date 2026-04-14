.global _start
.extern main
.extern u_exit

_start:
    bl main
#ifdef USER_KERNEL_MODE
    ret
#else
    bl u_exit
1:
    wfe
    b 1b
#endif
