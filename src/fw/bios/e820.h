#ifndef TUNIX_BOOT_FW_BIOS_E820_H
#define TUNIX_BOOT_FW_BIOS_E820_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/map.h"

/*
 * INT 15h AX=E820h cannot be called from long mode, so stage2 runs the loop
 * while still in real mode and leaves the raw entries in a buffer. This turns
 * that buffer into a memory map, and it is the only part of the arrangement
 * that has to be right about anything, which is why it is here and not in the
 * assembly.
 */

#define E820_ENTRY_BYTES 24U
#define E820_MAX_ENTRIES 128U

/* Type values as the interface defines them. Anything else is a type this
   loader has never heard of, and unknown memory is memory we do not touch. */
#define E820_TYPE_USABLE 1U
#define E820_TYPE_RESERVED 2U
#define E820_TYPE_ACPI_RECLAIMABLE 3U
#define E820_TYPE_ACPI_NVS 4U
#define E820_TYPE_BAD 5U

/* ACPI 3.0 added an attributes word; bit 0 clear means the firmware is telling
   us to ignore the entry. Firmware that predates it returns 20 bytes and never
   writes the word, so stage2 seeds it set. */
#define E820_ATTRIBUTE_VALID 0x1U

/* Returns false only when there is nothing usable to be had; entries that are
   individually nonsense are dropped and the rest are kept, because a single bad
   row is not a reason to refuse to boot. */
bool e820_parse(const void *buffer, uint32_t count, struct memory_map *out);

enum memory_kind e820_kind(uint32_t type);

#endif
