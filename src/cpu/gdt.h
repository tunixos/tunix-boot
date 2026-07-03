#ifndef TUNIX_BOOT_CPU_GDT_H
#define TUNIX_BOOT_CPU_GDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDT_SELECTOR_NULL 0x00U
#define GDT_SELECTOR_CODE64 0x08U
#define GDT_SELECTOR_DATA64 0x10U
#define GDT_SELECTOR_CODE32 0x18U
#define GDT_SELECTOR_DATA32 0x20U
#define GDT_SELECTOR_CODE16 0x28U
#define GDT_SELECTOR_DATA16 0x30U

#define GDT_ENTRY_COUNT 7U
#define GDT_ENTRY_BYTES 8U

/* What the lgdt operand looks like in memory. Packed because the processor
   reads it, not the compiler. */
struct __attribute__((packed)) gdt_pointer {
    uint16_t limit;
    uint64_t base;
};

/*
 * The loader needs three descriptor widths at once: 64-bit for the core,
 * 32-bit to get there, and 16-bit to drop back for a BIOS service call. They
 * live in one table so that switching mode never means reloading the GDT.
 */
void gdt_build(uint8_t *table, size_t capacity);

/* Encode one descriptor. False if the limit or flags cannot be represented. */
bool gdt_encode(uint8_t *entry, uint32_t base, uint32_t limit, uint8_t access,
                uint8_t flags);


#endif
