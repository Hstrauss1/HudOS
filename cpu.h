//Hudson Strauss
#ifndef CPU_H
#define CPU_H

// returns current exception level (0-3)
int cpu_get_el(void);

// installs the exception vector table into VBAR_EL2
void cpu_install_vectors(void);

// unmask IRQs in DAIF (clear the I bit)
void cpu_enable_irqs(void);

// mask IRQs in DAIF (set the I bit)
void cpu_disable_irqs(void);

#endif
