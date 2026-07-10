#ifndef TUNIX_BOOT_FS_EXT2_H
#define TUNIX_BOOT_FS_EXT2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block/block.h"

/*
 * ext2, read only.
 *
 * The counterpart to FAT: where FAT chains clusters, ext2 addresses blocks
 * through up to three levels of indirection, so the failure it invites is not a
 * loop but a pointer to a block that is not there. Every block number read off
 * the disk is checked against the volume before anything is read from it.
 *
 * This also reads ext3 and ext4 volumes that use no extents, since the parts
 * relied on here have not changed. An extent-mapped inode is refused rather
 * than misread.
 */

#define EXT2_SUPERBLOCK_OFFSET 1024U
#define EXT2_SUPERBLOCK_BYTES 1024U
#define EXT2_MAGIC 0xEF53U

#define EXT2_SUPER_INODE_COUNT_OFFSET 0U
#define EXT2_SUPER_BLOCK_COUNT_OFFSET 4U
#define EXT2_SUPER_FIRST_DATA_BLOCK_OFFSET 20U
#define EXT2_SUPER_LOG_BLOCK_SIZE_OFFSET 24U
#define EXT2_SUPER_BLOCKS_PER_GROUP_OFFSET 32U
#define EXT2_SUPER_INODES_PER_GROUP_OFFSET 40U
#define EXT2_SUPER_MAGIC_OFFSET 56U
#define EXT2_SUPER_REVISION_OFFSET 76U
#define EXT2_SUPER_INODE_BYTES_OFFSET 88U
#define EXT2_SUPER_INCOMPATIBLE_OFFSET 96U

#define EXT2_REVISION_ORIGINAL 0U
#define EXT2_ORIGINAL_INODE_BYTES 128U
#define EXT2_MIN_BLOCK_BYTES 1024U
#define EXT2_MAX_BLOCK_BYTES 65536U
#define EXT2_MAX_LOG_BLOCK_SIZE 6U

/* Extent-mapped inodes are a different on-disk layout for the block map, and
   reading one as a block list produces plausible garbage. */
#define EXT2_INCOMPATIBLE_EXTENTS 0x0040U
#define EXT2_INODE_FLAG_EXTENTS 0x00080000U

#define EXT2_GROUP_DESCRIPTOR_BYTES 32U
#define EXT2_GROUP_INODE_TABLE_OFFSET 8U

#define EXT2_ROOT_INODE 2U
#define EXT2_INODE_MODE_OFFSET 0U
#define EXT2_INODE_SIZE_LOW_OFFSET 4U
#define EXT2_INODE_FLAGS_OFFSET 32U
#define EXT2_INODE_BLOCKS_OFFSET 40U
#define EXT2_INODE_SIZE_HIGH_OFFSET 108U

#define EXT2_MODE_FORMAT_MASK 0xF000U
#define EXT2_MODE_DIRECTORY 0x4000U
#define EXT2_MODE_REGULAR 0x8000U

#define EXT2_DIRECT_BLOCKS 12U
#define EXT2_INDIRECT_INDEX 12U
#define EXT2_DOUBLE_INDIRECT_INDEX 13U
#define EXT2_TRIPLE_INDIRECT_INDEX 14U
#define EXT2_BLOCK_POINTERS 15U
#define EXT2_POINTER_BYTES 4U

#define EXT2_DIRENT_INODE_OFFSET 0U
#define EXT2_DIRENT_RECORD_LENGTH_OFFSET 4U
#define EXT2_DIRENT_NAME_LENGTH_OFFSET 6U
#define EXT2_DIRENT_NAME_OFFSET 8U
#define EXT2_DIRENT_MIN_BYTES 8U

#define EXT2_MAX_NAME_BYTES 255U
#define EXT2_MAX_PATH_BYTES 1024U

struct ext2_volume {
    struct block_device *device;
    uint32_t block_bytes;
    uint32_t pointers_per_block;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_bytes;
    uint32_t block_count;
    uint32_t inode_count;
    uint64_t group_table_offset;
};

struct ext2_file {
    struct ext2_volume *volume;
    uint64_t size;
    bool directory;
    uint32_t blocks[EXT2_BLOCK_POINTERS];
};

bool ext2_mount(struct block_device *device, struct ext2_volume *volume);

bool ext2_open(struct ext2_volume *volume, const char *path,
               struct ext2_file *out);

/* Holes read as zeroes, which is what they are. Reads past the end are refused. */
bool ext2_read(struct ext2_file *file, uint64_t offset, size_t length, void *out);

#endif
