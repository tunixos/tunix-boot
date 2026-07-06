#include "memory/map.h"
#include "util/checked.h"
#include "util/mem.h"

#define MEMORY_MAP_MAX_BOUNDARIES (MEMORY_MAP_MAX_REGIONS * 2U)

static uint64_t region_end(const struct memory_region *region) {
    return region->base + region->length;
}

void memory_map_clear(struct memory_map *map) {
    map->count = 0;
    map->truncated = false;
}

bool memory_map_add(struct memory_map *map, uint64_t base, uint64_t length,
                    enum memory_kind kind) {
    uint64_t end;
    if (length == 0) return false;
    if (!checked_add_u64(base, length, &end)) return false;

    if (map->count == MEMORY_MAP_MAX_REGIONS) {
        map->truncated = true;
        return false;
    }

    map->regions[map->count].base = base;
    map->regions[map->count].length = length;
    map->regions[map->count].kind = kind;
    map->count++;
    return true;
}

static size_t collect_boundaries(const struct memory_map *map, uint64_t *out) {
    size_t count = 0;
    for (size_t index = 0; index < map->count; index++) {
        out[count++] = map->regions[index].base;
        out[count++] = region_end(&map->regions[index]);
    }

    for (size_t index = 1; index < count; index++) {
        uint64_t value = out[index];
        size_t position = index;
        while (position > 0 && out[position - 1] > value) {
            out[position] = out[position - 1];
            position--;
        }
        out[position] = value;
    }

    size_t unique = 0;
    for (size_t index = 0; index < count; index++) {
        if (unique == 0 || out[unique - 1] != out[index]) out[unique++] = out[index];
    }
    return unique;
}

/* The kind that wins the half-open interval, or false if nothing covers it.
   Every boundary in the sweep came from some region edge, so an interval is
   either wholly inside a region or wholly outside it — no partial cases. */
static bool interval_kind(const struct memory_map *map, uint64_t start,
                          uint64_t end, enum memory_kind *out) {
    bool covered = false;
    for (size_t index = 0; index < map->count; index++) {
        const struct memory_region *region = &map->regions[index];
        if (region->base > start || region_end(region) < end) continue;
        if (!covered || region->kind > *out) *out = region->kind;
        covered = true;
    }
    return covered;
}

void memory_map_finalize(struct memory_map *map) {
    if (map->count == 0) return;

    uint64_t boundaries[MEMORY_MAP_MAX_BOUNDARIES];
    size_t boundary_count = collect_boundaries(map, boundaries);

    /* A sweep over every elementary interval rather than pairwise splitting:
       it costs a pass over the regions per interval, on a list this short, and
       in exchange there is no overlap arrangement it can get wrong. */
    struct memory_region result[MEMORY_MAP_MAX_REGIONS];
    size_t result_count = 0;
    bool truncated = map->truncated;

    for (size_t index = 0; index + 1 < boundary_count; index++) {
        uint64_t start = boundaries[index];
        uint64_t end = boundaries[index + 1];
        enum memory_kind kind;
        if (!interval_kind(map, start, end, &kind)) continue;

        if (result_count > 0 && result[result_count - 1].kind == kind &&
            region_end(&result[result_count - 1]) == start) {
            result[result_count - 1].length += end - start;
            continue;
        }
        if (result_count == MEMORY_MAP_MAX_REGIONS) {
            truncated = true;
            break;
        }
        result[result_count].base = start;
        result[result_count].length = end - start;
        result[result_count].kind = kind;
        result_count++;
    }

    memcpy(map->regions, result, result_count * sizeof result[0]);
    map->count = result_count;
    map->truncated = truncated;
}

uint64_t memory_map_total(const struct memory_map *map, enum memory_kind kind) {
    uint64_t total = 0;
    for (size_t index = 0; index < map->count; index++) {
        if (map->regions[index].kind == kind) total += map->regions[index].length;
    }
    return total;
}

const struct memory_region *memory_map_find(const struct memory_map *map,
                                            uint64_t address) {
    for (size_t index = 0; index < map->count; index++) {
        const struct memory_region *region = &map->regions[index];
        if (address >= region->base && address < region_end(region)) return region;
    }
    return NULL;
}

bool memory_map_find_free(const struct memory_map *map, uint64_t size,
                          uint64_t alignment, uint64_t minimum, uint64_t *out) {
    if (size == 0) return false;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return false;

    for (size_t index = 0; index < map->count; index++) {
        const struct memory_region *region = &map->regions[index];
        if (region->kind != MEMORY_KIND_USABLE) continue;

        uint64_t start = region->base > minimum ? region->base : minimum;
        if (!checked_align_up_u64(start, alignment, &start)) continue;
        if (start < region->base) continue;

        uint64_t finish;
        if (!checked_add_u64(start, size, &finish)) continue;
        if (finish > region_end(region)) continue;

        *out = start;
        return true;
    }
    return false;
}

const char *memory_kind_name(enum memory_kind kind) {
    switch (kind) {
        case MEMORY_KIND_USABLE:                 return "usable";
        case MEMORY_KIND_BOOTLOADER_RECLAIMABLE: return "bootloader";
        case MEMORY_KIND_ACPI_RECLAIMABLE:       return "acpi";
        case MEMORY_KIND_ACPI_NVS:               return "acpi-nvs";
        case MEMORY_KIND_FRAMEBUFFER:            return "framebuffer";
        case MEMORY_KIND_KERNEL:                 return "kernel";
        case MEMORY_KIND_RESERVED:               return "reserved";
        case MEMORY_KIND_BAD:                    return "bad";
    }
    return "unknown";
}
