#include "block/ata.h"
#include "block/partition.h"
#include "boot/core.h"
#include "cpu/cpuid.h"
#include "fs/fat.h"
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

/* Where the boot record keeps the mark that says it is one. */
#define BOOT_RECORD_SIGNATURE_OFFSET 510U
#define BOOT_RECORD_SIGNATURE 0xAA55U

static struct memory_map memory;
static struct arena loader_arena;
static struct ata_channel boot_channel;
static struct block_device boot_disk;
static uint8_t boot_record[BLOCK_SECTOR_BYTES_DEFAULT];
static struct partition_table partitions;
static struct partition_view boot_view;
static struct block_device boot_partition;
static struct fat_volume boot_volume;

/* The file the image carries, read back to prove the whole path works. */
#define PROBE_PATH "/tunix.cfg"
#define PROBE_BYTES 64U

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

/* ATA is driven directly rather than through the firmware. Port I/O works the
   same under BIOS and UEFI and from long mode, so this is one driver instead of
   two, and no path here has to leave long mode to read a sector. */
static bool attach_boot_disk(void) {
    if (!ata_probe(ata_port_io(), ATA_PRIMARY_IO_BASE, ATA_PRIMARY_CONTROL_BASE,
                   false, &boot_channel, &boot_disk)) {
        return false;
    }
    LOG_INFO("disk %s, %u sectors, lba48 %s", boot_disk.name,
             boot_disk.sector_count, boot_channel.lba48 ? "yes" : "no");

    if (!block_read(&boot_disk, 0, 1, boot_record)) return false;

    uint16_t signature =
        (uint16_t)(boot_record[BOOT_RECORD_SIGNATURE_OFFSET] |
                   (boot_record[BOOT_RECORD_SIGNATURE_OFFSET + 1U] << 8));
    if (signature != BOOT_RECORD_SIGNATURE) {
        LOG_ERROR("the disk read back is not the one we booted from");
        return false;
    }

    LOG_INFO("boot record verified through the ata driver");
    return true;
}

/* Mounts the first partition that turns out to hold a filesystem, rather than
   trusting the type byte: the byte is a hint that costs nothing to forge, and
   mounting is the only thing that actually answers the question. */
static bool mount_boot_filesystem(void) {
    if (!partition_table_read(&boot_disk, &partitions)) {
        LOG_ERROR("no partition table on the boot disk");
        return false;
    }
    LOG_INFO("%u partitions, %s table", (uint64_t)partitions.count,
             partitions.gpt ? "gpt" : "mbr");

    for (unsigned index = 0; index < partitions.count; index++) {
        const struct partition *entry = &partitions.entries[index];
        if (!partition_open(&boot_disk, entry, &boot_view, &boot_partition))
            continue;
        if (!fat_mount(&boot_partition, &boot_volume)) continue;

        LOG_INFO("fat32 at lba %u, %u clusters of %u bytes", entry->start_lba,
                 (uint64_t)boot_volume.cluster_count,
                 (uint64_t)boot_volume.cluster_bytes);
        return true;
    }
    LOG_ERROR("no partition held a filesystem this loader can read");
    return false;
}

static bool read_probe_file(void) {
    struct fat_file file;
    if (!fat_open(&boot_volume, PROBE_PATH, &file)) {
        LOG_ERROR("%s is not on the filesystem", PROBE_PATH);
        return false;
    }
    if (file.size == 0 || file.size > PROBE_BYTES) {
        LOG_ERROR("%s is not the size it should be", PROBE_PATH);
        return false;
    }

    uint8_t contents[PROBE_BYTES];
    if (!fat_read(&file, 0, file.size, contents)) {
        LOG_ERROR("%s would not read", PROBE_PATH);
        return false;
    }

    contents[file.size - 1U] = '\0';
    LOG_INFO("read %u bytes from %s: %s", (uint64_t)file.size, PROBE_PATH,
             (const char *)contents);
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

    if (!attach_boot_disk()) {
        LOG_ERROR("no readable boot disk");
        return;
    }

    if (!mount_boot_filesystem()) return;
    if (!read_probe_file()) return;

    LOG_INFO("stage2 reached the core");
}
