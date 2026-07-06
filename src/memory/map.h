#ifndef TUNIX_BOOT_MEMORY_MAP_H
#define TUNIX_BOOT_MEMORY_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * What the firmware says about physical memory, after we stop believing all of
 * it. E820 maps from real machines arrive unsorted, with overlapping entries and
 * with regions that are usable according to one entry and reserved according to
 * another. Treating that as authoritative is how a loader ends up writing a
 * kernel over ACPI tables, so everything here exists to turn it into a sorted,
 * disjoint, conservative map.
 */

#define MEMORY_MAP_MAX_REGIONS 128U

/* Declared in order of precedence: where two entries claim the same byte, the
   later one in this list wins. Reordering this changes what the loader does. */
enum memory_kind {
    MEMORY_KIND_USABLE = 0,
    MEMORY_KIND_BOOTLOADER_RECLAIMABLE,
    MEMORY_KIND_ACPI_RECLAIMABLE,
    MEMORY_KIND_ACPI_NVS,
    MEMORY_KIND_FRAMEBUFFER,
    MEMORY_KIND_KERNEL,
    MEMORY_KIND_RESERVED,
    MEMORY_KIND_BAD,
};

struct memory_region {
    uint64_t base;
    uint64_t length;
    enum memory_kind kind;
};

struct memory_map {
    struct memory_region regions[MEMORY_MAP_MAX_REGIONS];
    size_t count;
    /* Set once entries have been dropped for want of room. The map is still
       usable; it just no longer describes all of memory, so a caller that needs
       to trust it must check. */
    bool truncated;
};

void memory_map_clear(struct memory_map *map);

/* Appends. Zero-length regions and any region whose end would wrap are refused,
   since both would survive as nonsense the rest of the loader has to defend
   against. Returns false when refused or when the map is full. */
bool memory_map_add(struct memory_map *map, uint64_t base, uint64_t length,
                    enum memory_kind kind);

/* Sorts, resolves overlaps, and merges what touches. After this the regions are
   ascending, disjoint, and no two adjacent ones share a kind.

   Where entries disagree about a byte, the more restrictive kind takes it —
   memory claimed by nobody is worth less than memory wrongly handed out. */
void memory_map_finalize(struct memory_map *map);

/* Total bytes of one kind. Only meaningful after finalize, since overlapping
   entries would otherwise be counted twice. */
uint64_t memory_map_total(const struct memory_map *map, enum memory_kind kind);

/* The region containing an address, or NULL. */
const struct memory_region *memory_map_find(const struct memory_map *map,
                                            uint64_t address);

/* The lowest aligned run of `size` usable bytes at or above `minimum`, or false
   if there is none. Does not modify the map; marking the result is the caller's
   business, so that the policy of what to do with it stays out of here. */
bool memory_map_find_free(const struct memory_map *map, uint64_t size,
                          uint64_t alignment, uint64_t minimum, uint64_t *out);

const char *memory_kind_name(enum memory_kind kind);

#endif
