#ifndef TUNIX_BOOT_CPU_CONTROL_H
#define TUNIX_BOOT_CPU_CONTROL_H

#include <stdint.h>

/* The control registers. As with cpu/port.h, only code that has to touch the
   processor directly includes this. */

static inline void control_set_page_table_root(uint64_t physical) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(physical) : "memory");
}

static inline uint64_t control_page_table_root(void) {
    uint64_t value;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(value));
    return value;
}

#endif
