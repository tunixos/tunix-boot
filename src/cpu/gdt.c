#include "cpu/gdt.h"
#include "util/mem.h"

#define ACCESS_PRESENT 0x80U
#define ACCESS_SEGMENT 0x10U
#define ACCESS_EXECUTABLE 0x08U
#define ACCESS_READ_WRITE 0x02U

#define FLAG_GRANULARITY_4K 0x80U
#define FLAG_SIZE_32 0x40U
#define FLAG_LONG_MODE 0x20U

#define LIMIT_MAX_20_BIT 0xFFFFFU
#define FLAGS_MASK 0xF0U

#define CODE_ACCESS (ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXECUTABLE | ACCESS_READ_WRITE)
#define DATA_ACCESS (ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_READ_WRITE)

#define FLAT_LIMIT LIMIT_MAX_20_BIT
#define REAL_MODE_LIMIT 0xFFFFU

bool gdt_encode(uint8_t *entry, uint32_t base, uint32_t limit, uint8_t access,
                uint8_t flags) {
    if (limit > LIMIT_MAX_20_BIT) return false;
    if ((flags & ~FLAGS_MASK) != 0) return false;

    entry[0] = (uint8_t)(limit & 0xFFU);
    entry[1] = (uint8_t)((limit >> 8) & 0xFFU);
    entry[2] = (uint8_t)(base & 0xFFU);
    entry[3] = (uint8_t)((base >> 8) & 0xFFU);
    entry[4] = (uint8_t)((base >> 16) & 0xFFU);
    entry[5] = access;
    entry[6] = (uint8_t)(((limit >> 16) & 0x0FU) | (flags & FLAGS_MASK));
    entry[7] = (uint8_t)((base >> 24) & 0xFFU);
    return true;
}

void gdt_build(uint8_t *table, size_t capacity) {
    if (capacity < GDT_ENTRY_COUNT * GDT_ENTRY_BYTES) return;
    memset(table, 0, capacity);

    /* A 64-bit code segment ignores base and limit; the long-mode flag is what
       distinguishes it, and setting the 32-bit size flag alongside it is
       illegal. */
    gdt_encode(table + GDT_SELECTOR_CODE64, 0, FLAT_LIMIT, CODE_ACCESS,
               FLAG_GRANULARITY_4K | FLAG_LONG_MODE);
    gdt_encode(table + GDT_SELECTOR_DATA64, 0, FLAT_LIMIT, DATA_ACCESS,
               FLAG_GRANULARITY_4K);
    gdt_encode(table + GDT_SELECTOR_CODE32, 0, FLAT_LIMIT, CODE_ACCESS,
               FLAG_GRANULARITY_4K | FLAG_SIZE_32);
    gdt_encode(table + GDT_SELECTOR_DATA32, 0, FLAT_LIMIT, DATA_ACCESS,
               FLAG_GRANULARITY_4K | FLAG_SIZE_32);
    /* Byte granularity and a 64 KiB limit: what a real-mode thunk needs when it
       returns through a protected-mode descriptor. */
    gdt_encode(table + GDT_SELECTOR_CODE16, 0, REAL_MODE_LIMIT, CODE_ACCESS, 0);
    gdt_encode(table + GDT_SELECTOR_DATA16, 0, REAL_MODE_LIMIT, DATA_ACCESS, 0);
}
