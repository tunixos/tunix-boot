#ifndef TUNIX_BOOT_CPU_PORT_H
#define TUNIX_BOOT_CPU_PORT_H

#include <stdint.h>

/* The x86 I/O space. Only code that talks to a device should include this;
   everything above takes its port access as a parameter so it can be tested. */

static inline uint8_t port_read_u8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline void port_write_u8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint16_t port_read_u16(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

#endif
