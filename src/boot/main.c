#include "boot/core.h"
#include "cpu/cpuid.h"
#include "log/log.h"
#include "memory/arena.h"

#define LOADER_ARENA_BYTES (1024ULL * 1024ULL)
#define LOADER_ARENA_ALIGNMENT (4096ULL)

/* Everything below a megabyte belongs to something already — the boot record,
   our stack, the page tables, the buffer stage2 left the memory map in — and
   none of that is described by the firmware map. */
#define LOADER_ARENA_FLOOR (1024ULL * 1024ULL)

/* The loader may only touch what it has mapped, and both backends bring up an
   identity map of the first four gigabytes and no more. */
#define LOADER_ADDRESS_LIMIT (4ULL * 1024ULL * 1024ULL * 1024ULL)

#define BYTES_PER_MIB (1024ULL * 1024ULL)

static struct memory_map memory;
static struct arena loader_arena;

static void report_features(const struct cpu_features *features) {
    LOG_INFO("cpu %s, %u physical / %u linear address bits",
             features->vendor,
             (uint64_t)features->physical_address_bits,
             (uint64_t)features->linear_address_bits);
    LOG_INFO("long mode %s, nx %s, 1g pages %s",
             features->long_mode ? "yes" : "no",
             features->no_execute ? "yes" : "no",
             features->gigabyte_pages ? "yes" : "no");
    LOG_INFO("smep %s, smap %s, avx %s",
             features->smep ? "yes" : "no",
             features->smap ? "yes" : "no",
             features->avx ? "yes" : "no");
}

static void report_memory(const struct memory_map *map) {
    for (size_t index = 0; index < map->count; index++) {
        const struct memory_region *region = &map->regions[index];
        LOG_DEBUG("  %x..%x %s", region->base, region->base + region->length,
                  memory_kind_name(region->kind));
    }
    LOG_INFO("memory: %u MiB usable in %u regions%s",
             memory_map_total(map, MEMORY_KIND_USABLE) / BYTES_PER_MIB,
             (uint64_t)map->count,
             map->truncated ? " (truncated)" : "");
}

static bool place_arena(const struct memory_map *map, struct arena *arena) {
    uint64_t base;
    if (!memory_map_find_free(map, LOADER_ARENA_BYTES, LOADER_ARENA_ALIGNMENT,
                              LOADER_ARENA_FLOOR, &base)) {
        return false;
    }
    if (base + LOADER_ARENA_BYTES > LOADER_ADDRESS_LIMIT) return false;

    arena_init(arena, (void *)(uintptr_t)base, LOADER_ARENA_BYTES);
    LOG_INFO("arena at %x, %u KiB", base, LOADER_ARENA_BYTES / 1024ULL);
    return true;
}

void boot_core(const struct fw_ops *fw) {
    LOG_INFO("tunix-boot on %s firmware", fw_name(fw));

    struct cpu_features features;
    cpu_features_detect(cpuid_execute, &features);
    report_features(&features);

    if (!features.long_mode) {
        LOG_ERROR("the processor does not report long mode");
        return;
    }

    if (!fw_mem_snapshot(fw, &memory)) {
        LOG_ERROR("the firmware described no usable memory");
        return;
    }
    report_memory(&memory);

    if (!place_arena(&memory, &loader_arena)) {
        LOG_ERROR("no room for the loader arena");
        return;
    }

    LOG_INFO("stage2 reached the core");
}
