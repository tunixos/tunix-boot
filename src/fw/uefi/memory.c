#include "fw/uefi/memory.h"
#include "util/checked.h"
#include "util/reader.h"

enum memory_kind uefi_memory_kind(uint32_t type, enum uefi_phase phase) {
    switch (type) {
        case UEFI_MEMORY_CONVENTIONAL:
            return MEMORY_KIND_USABLE;

        /* The loader's own image and pools. Free once the kernel has taken what
           it wants, which is exactly what reclaimable means. */
        case UEFI_MEMORY_LOADER_CODE:
        case UEFI_MEMORY_LOADER_DATA:
            return MEMORY_KIND_BOOTLOADER_RECLAIMABLE;

        /* The firmware's own code and heap. Still running before the exit, and
           ordinary free memory after it. */
        case UEFI_MEMORY_BOOT_SERVICES_CODE:
        case UEFI_MEMORY_BOOT_SERVICES_DATA:
            return phase == UEFI_AFTER_EXIT ? MEMORY_KIND_USABLE
                                            : MEMORY_KIND_RESERVED;

        case UEFI_MEMORY_ACPI_RECLAIM:
            return MEMORY_KIND_ACPI_RECLAIMABLE;
        case UEFI_MEMORY_ACPI_NVS:
            return MEMORY_KIND_ACPI_NVS;
        case UEFI_MEMORY_UNUSABLE:
            return MEMORY_KIND_BAD;

        /* Runtime services keep working after the exit and their memory is
           never ours; the same goes for anything that is not really memory. */
        case UEFI_MEMORY_RUNTIME_SERVICES_CODE:
        case UEFI_MEMORY_RUNTIME_SERVICES_DATA:
        case UEFI_MEMORY_MAPPED_IO:
        case UEFI_MEMORY_MAPPED_IO_PORT:
        case UEFI_MEMORY_PAL_CODE:
        case UEFI_MEMORY_PERSISTENT:
        case UEFI_MEMORY_RESERVED:
        default:
            return MEMORY_KIND_RESERVED;
    }
}

bool uefi_memory_parse(const void *descriptors, size_t total_bytes,
                       size_t descriptor_bytes, enum uefi_phase phase,
                       struct memory_map *out) {
    memory_map_clear(out);
    if (!descriptors || total_bytes == 0) return false;

    /* Both bounds matter: too small and the fields read past their own
       descriptor, too large and the stride walks off the buffer. */
    if (descriptor_bytes < UEFI_DESCRIPTOR_MIN_BYTES) return false;
    if (descriptor_bytes > UEFI_DESCRIPTOR_MAX_BYTES) return false;

    const uint8_t *base = (const uint8_t *)descriptors;
    for (size_t offset = 0; offset + descriptor_bytes <= total_bytes;
         offset += descriptor_bytes) {
        struct reader reader;
        reader_init(&reader, base + offset, descriptor_bytes);

        uint32_t type;
        uint64_t physical, pages;
        if (!reader_u32(&reader, &type)) break;
        if (!reader_seek(&reader, UEFI_DESCRIPTOR_PHYSICAL_OFFSET)) break;
        if (!reader_u64(&reader, &physical)) break;
        if (!reader_seek(&reader, UEFI_DESCRIPTOR_PAGES_OFFSET)) break;
        if (!reader_u64(&reader, &pages)) break;

        uint64_t length;
        if (!checked_mul_u64(pages, UEFI_PAGE_BYTES, &length)) continue;

        /* memory_map_add refuses zero lengths and ends that wrap, so a
           descriptor that is nonsense is dropped and the rest are kept. */
        memory_map_add(out, physical, length, uefi_memory_kind(type, phase));
    }

    memory_map_finalize(out);
    return memory_map_total(out, MEMORY_KIND_USABLE) > 0;
}
