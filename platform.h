//Hudson Strauss
#ifndef PLATFORM_H
#define PLATFORM_H

#if defined(PLATFORM_VIRT)

#define PLATFORM_NAME              "virt"
#define PLATFORM_BOARD_NAME        "QEMU virt"
#define PLATFORM_UART_BASE         0x09000000UL
#define PLATFORM_UART_IBRD         13
#define PLATFORM_UART_FBRD         1
#define PLATFORM_GICD_BASE         0x08000000UL
#define PLATFORM_GICC_BASE         0x08010000UL

#define PLATFORM_HAS_GPIO          0
#define PLATFORM_HAS_FRAMEBUFFER   0
#define PLATFORM_HAS_USB_KEYBOARD  0
#define PLATFORM_HAS_SD            0
#define PLATFORM_INIT_MMU          0
#define PLATFORM_INIT_IRQS         0
#define PLATFORM_USES_MMIO_TIMER   0

#else

#define PLATFORM_NAME              "rpi4"
#define PLATFORM_BOARD_NAME        "Raspberry Pi 4 (BCM2711)"
#define PLATFORM_UART_BASE         0xFE201000UL
#define PLATFORM_UART_IBRD         26
#define PLATFORM_UART_FBRD         3
#define PLATFORM_GICD_BASE         0xFF841000UL
#define PLATFORM_GICC_BASE         0xFF842000UL

#define PLATFORM_HAS_GPIO          1
#define PLATFORM_HAS_FRAMEBUFFER   1
#define PLATFORM_HAS_USB_KEYBOARD  1
#define PLATFORM_HAS_SD            1
#define PLATFORM_INIT_MMU          1
#define PLATFORM_INIT_IRQS         1
#define PLATFORM_USES_MMIO_TIMER   1

#endif

#endif
