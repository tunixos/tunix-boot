#ifndef TUNIX_BOOT_FS_FAT_H
#define TUNIX_BOOT_FS_FAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block/block.h"

/*
 * FAT32, read only.
 *
 * This is what a loader reads its configuration and its kernel from, so it has
 * to work on filesystems made by other people's tools. Every field that decides
 * where a read lands is checked against the volume it came from: a FAT is an
 * on-disk linked list, and a corrupt one is a loop or a jump into the middle of
 * somebody else's data, neither of which announces itself.
 */

#define FAT_BPB_BYTES_PER_SECTOR_OFFSET 11U
#define FAT_BPB_SECTORS_PER_CLUSTER_OFFSET 13U
#define FAT_BPB_RESERVED_SECTORS_OFFSET 14U
#define FAT_BPB_FAT_COUNT_OFFSET 16U
#define FAT_BPB_ROOT_ENTRY_COUNT_OFFSET 17U
#define FAT_BPB_TOTAL_SECTORS_16_OFFSET 19U
#define FAT_BPB_SECTORS_PER_FAT_16_OFFSET 22U
#define FAT_BPB_TOTAL_SECTORS_32_OFFSET 32U
#define FAT_BPB_SECTORS_PER_FAT_32_OFFSET 36U
#define FAT_BPB_ROOT_CLUSTER_OFFSET 44U
#define FAT_BPB_SIGNATURE_OFFSET 510U
#define FAT_BPB_SIGNATURE 0xAA55U

#define FAT_DIRECTORY_ENTRY_BYTES 32U
#define FAT_ENTRY_NAME_OFFSET 0U
#define FAT_ENTRY_NAME_BYTES 11U
#define FAT_ENTRY_ATTRIBUTES_OFFSET 11U
#define FAT_ENTRY_CLUSTER_HIGH_OFFSET 20U
#define FAT_ENTRY_CLUSTER_LOW_OFFSET 26U
#define FAT_ENTRY_SIZE_OFFSET 28U

#define FAT_ATTRIBUTE_READ_ONLY 0x01U
#define FAT_ATTRIBUTE_HIDDEN 0x02U
#define FAT_ATTRIBUTE_SYSTEM 0x04U
#define FAT_ATTRIBUTE_VOLUME_LABEL 0x08U
#define FAT_ATTRIBUTE_DIRECTORY 0x10U
/* A long-name fragment, not a file: the four flags together are a value no real
   entry can have, which is how long names were added without breaking readers
   that predate them. */
#define FAT_ATTRIBUTE_LONG_NAME 0x0FU

#define FAT_ENTRY_END_OF_DIRECTORY 0x00U
#define FAT_ENTRY_DELETED 0xE5U

/* Cluster numbers 0 and 1 are not clusters, so data starts at 2. */
#define FAT_FIRST_DATA_CLUSTER 2U
#define FAT_CLUSTER_MASK 0x0FFFFFFFU
#define FAT_CLUSTER_BAD 0x0FFFFFF7U
#define FAT_CLUSTER_END 0x0FFFFFF8U
#define FAT_FAT_ENTRY_BYTES 4U

#define FAT_MAX_PATH_BYTES 256U
#define FAT_MAX_NAME_BYTES 13U

struct fat_volume {
    struct block_device *device;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_bytes;
    uint64_t fat_start_sector;
    uint64_t data_start_sector;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    /* Clusters that exist. The bound every chain walk is checked against. */
    uint32_t cluster_count;
};

struct fat_file {
    struct fat_volume *volume;
    uint32_t first_cluster;
    uint32_t size;
    bool directory;
};

bool fat_mount(struct block_device *device, struct fat_volume *volume);

/* Absolute, slash-separated. Names are matched case-insensitively, as the
   filesystem itself does. */
bool fat_open(struct fat_volume *volume, const char *path, struct fat_file *out);

/* Bytes from within the file. Reads past the end are refused rather than
   zero-filled, so a truncated kernel image cannot look like a valid one. */
bool fat_read(struct fat_file *file, uint64_t offset, size_t length, void *out);

#endif
