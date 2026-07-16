#include "fs/ext2.h"
#include "util/checked.h"
#include "util/mem.h"
#include "util/reader.h"

#define PATH_SEPARATOR '/'
#define BLOCK_HOLE 0U

static bool read_u32_at(struct ext2_volume *volume, uint64_t offset,
                        uint32_t *out) {
    uint8_t raw[EXT2_POINTER_BYTES];
    if (!block_read_bytes(volume->device, offset, sizeof raw, raw)) return false;
    *out = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) |
           ((uint32_t)raw[3] << 24);
    return true;
}

static bool block_offset(const struct ext2_volume *volume, uint32_t block,
                         uint64_t *out) {
    /* Every block number here came off the disk. One past the end reads the
       next structure along and looks like data. */
    if (block == BLOCK_HOLE || block >= volume->block_count) return false;
    return checked_mul_u64(block, volume->block_bytes, out);
}

bool ext2_mount(struct block_device *device, struct ext2_volume *volume) {
    if (!block_device_valid(device) || !volume) return false;

    uint8_t super[EXT2_SUPERBLOCK_BYTES];
    if (!block_read_bytes(device, EXT2_SUPERBLOCK_OFFSET, sizeof super, super))
        return false;

    struct reader reader;
    reader_init(&reader, super, sizeof super);

    uint16_t magic, inode_bytes_16;
    uint32_t inode_count, block_count, first_data_block, log_block_size;
    uint32_t blocks_per_group, inodes_per_group, revision, incompatible;

    if (!reader_seek(&reader, EXT2_SUPER_MAGIC_OFFSET)) return false;
    if (!reader_u16(&reader, &magic)) return false;
    if (magic != EXT2_MAGIC) return false;

    if (!reader_seek(&reader, EXT2_SUPER_INODE_COUNT_OFFSET)) return false;
    if (!reader_u32(&reader, &inode_count)) return false;
    if (!reader_u32(&reader, &block_count)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_FIRST_DATA_BLOCK_OFFSET)) return false;
    if (!reader_u32(&reader, &first_data_block)) return false;
    if (!reader_u32(&reader, &log_block_size)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_BLOCKS_PER_GROUP_OFFSET)) return false;
    if (!reader_u32(&reader, &blocks_per_group)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_INODES_PER_GROUP_OFFSET)) return false;
    if (!reader_u32(&reader, &inodes_per_group)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_REVISION_OFFSET)) return false;
    if (!reader_u32(&reader, &revision)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_INODE_BYTES_OFFSET)) return false;
    if (!reader_u16(&reader, &inode_bytes_16)) return false;
    if (!reader_seek(&reader, EXT2_SUPER_INCOMPATIBLE_OFFSET)) return false;
    if (!reader_u32(&reader, &incompatible)) return false;

    if (log_block_size > EXT2_MAX_LOG_BLOCK_SIZE) return false;
    uint32_t block_bytes = EXT2_MIN_BLOCK_BYTES << log_block_size;
    if (block_bytes < EXT2_MIN_BLOCK_BYTES || block_bytes > EXT2_MAX_BLOCK_BYTES)
        return false;
    if (block_bytes % device->sector_bytes != 0) return false;

    if (incompatible & EXT2_INCOMPATIBLE_EXTENTS) return false;
    if (blocks_per_group == 0 || inodes_per_group == 0) return false;
    if (block_count == 0 || inode_count == 0) return false;

    uint32_t inode_bytes = revision == EXT2_REVISION_ORIGINAL
                               ? EXT2_ORIGINAL_INODE_BYTES
                               : inode_bytes_16;
    if (inode_bytes < EXT2_ORIGINAL_INODE_BYTES) return false;
    if (inode_bytes > block_bytes) return false;
    /* A power of two, so an inode never straddles a block boundary. */
    if ((inode_bytes & (inode_bytes - 1U)) != 0) return false;

    uint64_t volume_bytes;
    if (!checked_mul_u64(block_count, block_bytes, &volume_bytes)) return false;
    if (device->sector_count != 0 &&
        volume_bytes > device->sector_count * (uint64_t)device->sector_bytes) {
        return false;
    }

    /* The group table follows the superblock's own block. With 1 KiB blocks the
       superblock is block 1, so the table is block 2; larger blocks put the
       superblock inside block 0 and the table in block 1. */
    uint64_t table_block = (uint64_t)first_data_block + 1U;
    volume->device = device;
    volume->block_bytes = block_bytes;
    volume->pointers_per_block = block_bytes / EXT2_POINTER_BYTES;
    volume->blocks_per_group = blocks_per_group;
    volume->inodes_per_group = inodes_per_group;
    volume->inode_bytes = inode_bytes;
    volume->block_count = block_count;
    volume->inode_count = inode_count;

    if (!checked_mul_u64(table_block, block_bytes, &volume->group_table_offset))
        return false;
    return true;
}

/* The disk block holding file block `index`, or false for a hole or a pointer
   the volume cannot contain. */
static bool resolve_block(struct ext2_volume *volume, const uint32_t *blocks,
                          uint64_t index, uint32_t *out) {
    const uint64_t per_block = volume->pointers_per_block;

    if (index < EXT2_DIRECT_BLOCKS) {
        *out = blocks[index];
        return true;
    }
    index -= EXT2_DIRECT_BLOCKS;

    /* Each level is the same walk one link longer, so the depth is worked out
       first and then followed, rather than written out three times. */
    uint64_t span = per_block;
    unsigned depth = 1;
    unsigned slot = EXT2_INDIRECT_INDEX;

    while (depth <= 3) {
        if (index < span) break;
        index -= span;
        if (!checked_mul_u64(span, per_block, &span)) return false;
        depth++;
        slot++;
    }
    if (depth > 3) return false;

    uint32_t block = blocks[slot];
    while (depth > 0) {
        if (block == BLOCK_HOLE) {
            *out = BLOCK_HOLE;
            return true;
        }
        span /= per_block;
        uint64_t within = index / span;
        index %= span;

        uint64_t offset;
        if (!block_offset(volume, block, &offset)) return false;
        if (!checked_add_u64(offset, within * EXT2_POINTER_BYTES, &offset))
            return false;
        if (!read_u32_at(volume, offset, &block)) return false;
        depth--;
    }
    *out = block;
    return true;
}

static bool read_blocks(struct ext2_volume *volume, const uint32_t *blocks,
                        uint64_t offset, size_t length, void *out) {
    uint8_t *cursor = (uint8_t *)out;

    while (length > 0) {
        uint64_t index = offset / volume->block_bytes;
        uint32_t into_block = (uint32_t)(offset % volume->block_bytes);
        uint32_t available = volume->block_bytes - into_block;
        size_t take = available < length ? available : length;

        uint32_t block;
        if (!resolve_block(volume, blocks, index, &block)) return false;

        if (block == BLOCK_HOLE) {
            /* A hole is zeroes on purpose, not an error: ext2 stores a sparse
               file by not storing it. */
            memset(cursor, 0, take);
        } else {
            uint64_t at;
            if (!block_offset(volume, block, &at)) return false;
            if (!checked_add_u64(at, into_block, &at)) return false;
            if (!block_read_bytes(volume->device, at, take, cursor)) return false;
        }

        cursor += take;
        offset += take;
        length -= take;
    }
    return true;
}

static bool read_inode(struct ext2_volume *volume, uint32_t inode,
                       struct ext2_file *out) {
    if (inode == 0 || inode > volume->inode_count) return false;

    uint32_t group = (inode - 1U) / volume->inodes_per_group;
    uint32_t within = (inode - 1U) % volume->inodes_per_group;

    uint64_t descriptor;
    if (!checked_mul_u64(group, EXT2_GROUP_DESCRIPTOR_BYTES, &descriptor))
        return false;
    if (!checked_add_u64(volume->group_table_offset, descriptor, &descriptor))
        return false;

    uint32_t table_block;
    if (!read_u32_at(volume, descriptor + EXT2_GROUP_INODE_TABLE_OFFSET,
                     &table_block)) return false;

    uint64_t at;
    if (!block_offset(volume, table_block, &at)) return false;
    if (!checked_add_u64(at, (uint64_t)within * volume->inode_bytes, &at))
        return false;

    uint8_t raw[EXT2_ORIGINAL_INODE_BYTES];
    if (!block_read_bytes(volume->device, at, sizeof raw, raw)) return false;

    struct reader reader;
    reader_init(&reader, raw, sizeof raw);

    uint16_t mode;
    uint32_t size_low, flags;
    if (!reader_seek(&reader, EXT2_INODE_MODE_OFFSET)) return false;
    if (!reader_u16(&reader, &mode)) return false;
    if (!reader_seek(&reader, EXT2_INODE_SIZE_LOW_OFFSET)) return false;
    if (!reader_u32(&reader, &size_low)) return false;
    if (!reader_seek(&reader, EXT2_INODE_FLAGS_OFFSET)) return false;
    if (!reader_u32(&reader, &flags)) return false;
    if (flags & EXT2_INODE_FLAG_EXTENTS) return false;

    if (!reader_seek(&reader, EXT2_INODE_BLOCKS_OFFSET)) return false;
    for (unsigned index = 0; index < EXT2_BLOCK_POINTERS; index++) {
        if (!reader_u32(&reader, &out->blocks[index])) return false;
    }

    uint16_t format = mode & EXT2_MODE_FORMAT_MASK;
    out->volume = volume;
    out->directory = format == EXT2_MODE_DIRECTORY;
    /* The high half of the size is a directory ACL field on directories, so it
       is only a size for regular files. */
    out->size = size_low;
    if (format == EXT2_MODE_REGULAR) {
        uint32_t size_high;
        if (!reader_seek(&reader, EXT2_INODE_SIZE_HIGH_OFFSET)) return false;
        if (!reader_u32(&reader, &size_high)) return false;
        out->size |= (uint64_t)size_high << 32;
        return true;
    }
    /* Anything that is neither is a device node or a symlink, and this loader
       has nothing to do with one. */
    return format == EXT2_MODE_DIRECTORY;
}

static bool name_matches(const uint8_t *entry_name, uint8_t entry_length,
                         const char *name, size_t length) {
    if (entry_length != length) return false;
    for (size_t index = 0; index < length; index++) {
        if (entry_name[index] != (uint8_t)name[index]) return false;
    }
    return true;
}

static bool find_in_directory(struct ext2_volume *volume,
                              const struct ext2_file *directory,
                              const char *name, size_t length, uint32_t *out) {
    uint64_t offset = 0;
    uint8_t entry[EXT2_DIRENT_MIN_BYTES + EXT2_MAX_NAME_BYTES];

    while (offset < directory->size) {
        uint64_t remaining = directory->size - offset;
        if (remaining < EXT2_DIRENT_MIN_BYTES) return false;

        if (!read_blocks(volume, directory->blocks, offset,
                         EXT2_DIRENT_MIN_BYTES, entry)) return false;

        uint32_t inode = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8) |
                         ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);
        uint16_t record = (uint16_t)(entry[EXT2_DIRENT_RECORD_LENGTH_OFFSET] |
                                     (entry[EXT2_DIRENT_RECORD_LENGTH_OFFSET + 1U] << 8));
        uint8_t name_length = entry[EXT2_DIRENT_NAME_LENGTH_OFFSET];

        /* A record that does not advance is a directory that never ends, and a
           record longer than what is left runs off the end of the inode. */
        if (record < EXT2_DIRENT_MIN_BYTES || record > remaining) return false;
        if ((uint64_t)EXT2_DIRENT_MIN_BYTES + name_length > record) return false;

        if (inode != 0 && name_length == length) {
            if (!read_blocks(volume, directory->blocks,
                             offset + EXT2_DIRENT_NAME_OFFSET, name_length, entry))
                return false;
            if (name_matches(entry, name_length, name, length)) {
                *out = inode;
                return true;
            }
        }
        offset += record;
    }
    return false;
}

bool ext2_open(struct ext2_volume *volume, const char *path,
               struct ext2_file *out) {
    if (!volume || !path || !out) return false;

    struct ext2_file current;
    if (!read_inode(volume, EXT2_ROOT_INODE, &current)) return false;
    if (!current.directory) return false;

    size_t index = 0;
    while (path[index] == PATH_SEPARATOR) index++;

    while (path[index] != '\0') {
        if (index >= EXT2_MAX_PATH_BYTES) return false;
        if (!current.directory) return false;

        size_t start = index;
        while (path[index] != '\0' && path[index] != PATH_SEPARATOR) index++;
        size_t length = index - start;
        if (length == 0 || length > EXT2_MAX_NAME_BYTES) return false;

        uint32_t inode;
        if (!find_in_directory(volume, &current, path + start, length, &inode))
            return false;
        if (!read_inode(volume, inode, &current)) return false;

        while (path[index] == PATH_SEPARATOR) index++;
    }

    *out = current;
    return true;
}

bool ext2_read(struct ext2_file *file, uint64_t offset, size_t length,
               void *out) {
    if (!file || !file->volume || !out) return false;
    if (file->directory) return false;
    if (length == 0) return true;

    uint64_t end;
    if (!checked_add_u64(offset, length, &end)) return false;
    if (end > file->size) return false;

    return read_blocks(file->volume, file->blocks, offset, length, out);
}
