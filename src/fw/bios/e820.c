#include "fw/bios/e820.h"
#include "util/checked.h"
#include "util/reader.h"

enum memory_kind e820_kind(uint32_t type) {
    switch (type) {
        case E820_TYPE_USABLE:            return MEMORY_KIND_USABLE;
        case E820_TYPE_ACPI_RECLAIMABLE:  return MEMORY_KIND_ACPI_RECLAIMABLE;
        case E820_TYPE_ACPI_NVS:          return MEMORY_KIND_ACPI_NVS;
        case E820_TYPE_BAD:               return MEMORY_KIND_BAD;
        case E820_TYPE_RESERVED:          return MEMORY_KIND_RESERVED;
        default:                          return MEMORY_KIND_RESERVED;
    }
}

bool e820_parse(const void *buffer, uint32_t count, struct memory_map *out) {
    memory_map_clear(out);
    if (!buffer || count == 0) return false;
    if (count > E820_MAX_ENTRIES) count = E820_MAX_ENTRIES;

    size_t length;
    if (!checked_mul_size(count, E820_ENTRY_BYTES, &length)) return false;

    struct reader reader;
    reader_init(&reader, buffer, length);

    for (uint32_t index = 0; index < count; index++) {
        uint64_t base, region_length;
        uint32_t type, attributes;
        if (!reader_u64(&reader, &base)) break;
        if (!reader_u64(&reader, &region_length)) break;
        if (!reader_u32(&reader, &type)) break;
        if (!reader_u32(&reader, &attributes)) break;

        if ((attributes & E820_ATTRIBUTE_VALID) == 0) continue;
        /* memory_map_add refuses zero length and ends that wrap, so the two
           ways firmware lies about a region are already handled there. */
        memory_map_add(out, base, region_length, e820_kind(type));
    }

    memory_map_finalize(out);
    return memory_map_total(out, MEMORY_KIND_USABLE) > 0;
}
