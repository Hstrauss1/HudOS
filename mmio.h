//Hudson Strauss
//BroadcomSOC chip declarations  https://pip-assets.raspberrypi.com/categories/545-raspberry-pi-4-model-b/documents/RP-008248-DS-1-bcm2711-peripherals.pdf?disposition=inline
#ifndef MMIO_H
#define MMIO_H

#include "platform.h"

#define UART0_BASE PLATFORM_UART_BASE  //base
#define UART0_DR   (*(volatile unsigned int *)(UART0_BASE + 0x00)) //Data Reg I/O
#define UART0_FR   (*(volatile unsigned int *)(UART0_BASE + 0x18)) //Flag Status Reg bit4 recieve empty 5 transmit full
#define UART0_IBRD (*(volatile unsigned int *)(UART0_BASE + 0x24)) //baud rate
#define UART0_FBRD (*(volatile unsigned int *)(UART0_BASE + 0x28)) //
#define UART0_LCRH (*(volatile unsigned int *)(UART0_BASE + 0x2C)) //Line Control packet format
#define UART0_CR   (*(volatile unsigned int *)(UART0_BASE + 0x30)) //on/off switch
#define UART0_ICR  (*(volatile unsigned int *)(UART0_BASE + 0x44)) //reset interrupt state

// GPIO registers  (BCM2711 section 5.2)
#define GPIO_BASE   0xFE200000UL
#define GPFSEL0     (*(volatile unsigned int *)(GPIO_BASE + 0x00)) //function select pins 0-9
#define GPFSEL1     (*(volatile unsigned int *)(GPIO_BASE + 0x04)) //function select pins 10-19
#define GPFSEL2     (*(volatile unsigned int *)(GPIO_BASE + 0x08)) //function select pins 20-29
#define GPFSEL3     (*(volatile unsigned int *)(GPIO_BASE + 0x0C)) //function select pins 30-39
#define GPFSEL4     (*(volatile unsigned int *)(GPIO_BASE + 0x10)) //function select pins 40-49
#define GPFSEL5     (*(volatile unsigned int *)(GPIO_BASE + 0x14)) //function select pins 50-57
#define GPSET0      (*(volatile unsigned int *)(GPIO_BASE + 0x1C)) //output set pins 0-31
#define GPSET1      (*(volatile unsigned int *)(GPIO_BASE + 0x20)) //output set pins 32-57
#define GPCLR0      (*(volatile unsigned int *)(GPIO_BASE + 0x28)) //output clear pins 0-31
#define GPCLR1      (*(volatile unsigned int *)(GPIO_BASE + 0x2C)) //output clear pins 32-57
#define GPLEV0      (*(volatile unsigned int *)(GPIO_BASE + 0x34)) //pin level pins 0-31
#define GPLEV1      (*(volatile unsigned int *)(GPIO_BASE + 0x38)) //pin level pins 32-57

// System Timer (BCM2711 section 10.2) - 1MHz free-running counter
#define TIMER_BASE  0xFE003000UL
#define TIMER_CS    (*(volatile unsigned int *)(TIMER_BASE + 0x00)) //control/status
#define TIMER_CLO   (*(volatile unsigned int *)(TIMER_BASE + 0x04)) //counter lower 32 bits
#define TIMER_CHI   (*(volatile unsigned int *)(TIMER_BASE + 0x08)) //counter upper 32 bits
#define TIMER_C1    (*(volatile unsigned int *)(TIMER_BASE + 0x10)) //compare channel 1
#define TIMER_C3    (*(volatile unsigned int *)(TIMER_BASE + 0x18)) //compare channel 3

// Legacy interrupt controller (BCM2711 section 7.5)
#define IRQ_BASE        0xFE00B000UL
#define IRQ_PENDING_1   (*(volatile unsigned int *)(IRQ_BASE + 0x204))
#define IRQ_PENDING_2   (*(volatile unsigned int *)(IRQ_BASE + 0x208))
#define IRQ_ENABLE_1    (*(volatile unsigned int *)(IRQ_BASE + 0x210))
#define IRQ_ENABLE_2    (*(volatile unsigned int *)(IRQ_BASE + 0x214))
#define IRQ_DISABLE_1   (*(volatile unsigned int *)(IRQ_BASE + 0x21C))
#define IRQ_DISABLE_2   (*(volatile unsigned int *)(IRQ_BASE + 0x220))

// ARM local peripherals (BCM2711 ARM peripherals)
#define ARM_LOCAL_BASE          0xFF800000UL
#define ARM_LOCAL_TIMER_CNTRL0  (*(volatile unsigned int *)(ARM_LOCAL_BASE + 0x40)) //core 0 timer IRQ control
#define ARM_LOCAL_IRQ_SOURCE0   (*(volatile unsigned int *)(ARM_LOCAL_BASE + 0x60)) //core 0 IRQ source
// Timer IRQ control bits:
//   bit 0 = CNTPSIRQ (secure physical timer)
//   bit 1 = CNTPNSIRQ (non-secure physical timer)
//   bit 2 = CNTHPIRQ (hypervisor physical timer)
//   bit 3 = CNTVIRQ (virtual timer)
#define LOCAL_TIMER_HP_IRQ      (1 << 2) //hypervisor physical timer (EL2)

// GIC-400 (BCM2711, used by QEMU raspi4b for interrupt routing)
#define GIC_BASE        PLATFORM_GICD_BASE
#define GICD_CTLR       (*(volatile unsigned int *)(GIC_BASE + 0x000))  //distributor control
#define GICD_IGROUPR(n)   (*(volatile unsigned int *)(GIC_BASE + 0x080 + (n)*4)) //interrupt group
#define GICD_ISENABLER(n) (*(volatile unsigned int *)(GIC_BASE + 0x100 + (n)*4)) //set-enable
#define GICD_ICENABLER(n) (*(volatile unsigned int *)(GIC_BASE + 0x180 + (n)*4)) //clear-enable
#define GICD_ISPENDR(n)   (*(volatile unsigned int *)(GIC_BASE + 0x200 + (n)*4)) //set-pending
#define GICD_IPRIORITYR(n) (*(volatile unsigned int *)(GIC_BASE + 0x400 + (n)*4)) //priority
#define GICD_ITARGETSR(n)  (*(volatile unsigned int *)(GIC_BASE + 0x800 + (n)*4)) //target CPU
#define GICC_BASE       PLATFORM_GICC_BASE
#define GICC_CTLR       (*(volatile unsigned int *)(GICC_BASE + 0x000))  //CPU interface control
#define GICC_PMR        (*(volatile unsigned int *)(GICC_BASE + 0x004))  //priority mask
#define GICC_IAR        (*(volatile unsigned int *)(GICC_BASE + 0x00C))  //interrupt acknowledge
#define GICC_EOIR       (*(volatile unsigned int *)(GICC_BASE + 0x010))  //end of interrupt

// PPI interrupt IDs for ARM generic timers
#define GIC_PPI_HP_TIMER  26  // hypervisor physical timer

// UART0 PL011 interrupt registers
#define UART0_IMSC  (*(volatile unsigned int *)(UART0_BASE + 0x38)) //interrupt mask set/clear
#define UART0_MIS   (*(volatile unsigned int *)(UART0_BASE + 0x40)) //masked interrupt status

// Mailbox (VideoCore communication, BCM2711 section 3)
#define MBOX_BASE       0xFE00B880UL
#define MBOX_READ       (*(volatile unsigned int *)(MBOX_BASE + 0x00))
#define MBOX_STATUS     (*(volatile unsigned int *)(MBOX_BASE + 0x18))
#define MBOX_WRITE      (*(volatile unsigned int *)(MBOX_BASE + 0x20))
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000
#define MBOX_CH_PROP    8  // property tags (ARM to VC)

// GPIO function select values
#define GPIO_FUNC_INPUT  0
#define GPIO_FUNC_OUTPUT 1
#define GPIO_FUNC_ALT0   4
#define GPIO_FUNC_ALT1   5
#define GPIO_FUNC_ALT2   6
#define GPIO_FUNC_ALT3   7
#define GPIO_FUNC_ALT4   3
#define GPIO_FUNC_ALT5   2

#endif
