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

/* The extended feature register. Long mode was enabled through it before any
   of this ran; what is left is the bit that makes the no-execute flag legal. */
#define EFER_MSR 0xC0000080U
#define EFER_NO_EXECUTE_ENABLE (1ULL << 11)

static inline uint64_t control_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (uint64_t)low | ((uint64_t)high << 32);
}

static inline void control_write_msr(uint32_t msr, uint64_t value) {
    __asm__ __volatile__("wrmsr"
                         :
                         : "c"(msr), "a"((uint32_t)value),
                           "d"((uint32_t)(value >> 32)));
}

/* Until this is set, bit 63 of a page table entry is a *reserved* bit, and a
   page marked no-execute faults on the first access rather than only on an
   instruction fetch. */
static inline void control_enable_no_execute(void) {
    control_write_msr(EFER_MSR,
                      control_read_msr(EFER_MSR) | EFER_NO_EXECUTE_ENABLE);
}

static inline uint64_t control_read_timestamp(void) {
    uint32_t low, high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return (uint64_t)low | ((uint64_t)high << 32);
}

#endif
