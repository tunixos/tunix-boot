#ifndef TUNIX_BOOT_FW_UEFI_MEMORY_H
#define TUNIX_BOOT_FW_UEFI_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/map.h"

/*
 * Turning what GetMemoryMap returns into the map the rest of the loader uses.
 *
 * The counterpart to e820.c, and the reason `struct fw_ops` exists: above this
 * point nothing knows which firmware described the machine.
 *
 * UEFI's map is more detailed than E820's and needs more interpretation, not
 * less. Most of it is memory the firmware is using and will stop using the
 * moment boot services end, so what a region *is* depends on when you ask —
 * which is why this takes that as an argument rather than guessing.
 */

/* Fixed by the specification and unrelated to the host page size. */
#define UEFI_PAGE_BYTES 4096ULL

/* Offsets into EFI_MEMORY_DESCRIPTOR. Read by offset rather than through a
   struct because the firmware reports its own descriptor size, which may be
   larger than these fields — see uefi_memory_parse. */
#define UEFI_DESCRIPTOR_TYPE_OFFSET 0U
#define UEFI_DESCRIPTOR_PHYSICAL_OFFSET 8U
#define UEFI_DESCRIPTOR_VIRTUAL_OFFSET 16U
#define UEFI_DESCRIPTOR_PAGES_OFFSET 24U
#define UEFI_DESCRIPTOR_ATTRIBUTE_OFFSET 32U
#define UEFI_DESCRIPTOR_MIN_BYTES 40U
#define UEFI_DESCRIPTOR_MAX_BYTES 1024U

#define UEFI_MEMORY_RESERVED 0U
#define UEFI_MEMORY_LOADER_CODE 1U
#define UEFI_MEMORY_LOADER_DATA 2U
#define UEFI_MEMORY_BOOT_SERVICES_CODE 3U
#define UEFI_MEMORY_BOOT_SERVICES_DATA 4U
#define UEFI_MEMORY_RUNTIME_SERVICES_CODE 5U
#define UEFI_MEMORY_RUNTIME_SERVICES_DATA 6U
#define UEFI_MEMORY_CONVENTIONAL 7U
#define UEFI_MEMORY_UNUSABLE 8U
#define UEFI_MEMORY_ACPI_RECLAIM 9U
#define UEFI_MEMORY_ACPI_NVS 10U
#define UEFI_MEMORY_MAPPED_IO 11U
#define UEFI_MEMORY_MAPPED_IO_PORT 12U
#define UEFI_MEMORY_PAL_CODE 13U
#define UEFI_MEMORY_PERSISTENT 14U

/*
 * Whether boot services have been exited yet. Before they have, the memory the
 * firmware runs from is still in use and must be left alone; after, most of it
 * becomes ordinary free memory. Getting this backwards either wastes half of
 * RAM or overwrites the firmware while it is still running.
 */
enum uefi_phase {
    UEFI_BEFORE_EXIT,
    UEFI_AFTER_EXIT,
};

enum memory_kind uefi_memory_kind(uint32_t type, enum uefi_phase phase);

/*
 * `descriptor_bytes` comes from the firmware and is *not* sizeof anything this
 * code declares: the specification allows a larger descriptor than the fields
 * defined so far, and stepping by the size of a struct instead of the reported
 * one reads every entry after the first from the wrong place.
 */
bool uefi_memory_parse(const void *descriptors, size_t total_bytes,
                       size_t descriptor_bytes, enum uefi_phase phase,
                       struct memory_map *out);

#endif
