#include "fs/fat.h"
#include "util/checked.h"
#include "util/mem.h"
#include "util/reader.h"

#define PATH_SEPARATOR '/'
#define NAME_PADDING ' '
#define EXTENSION_SEPARATOR '.'
#define NAME_BASE_BYTES 8U
#define NAME_EXTENSION_BYTES 3U

static uint8_t upper(uint8_t value) {
    if (value >= 'a' && value <= 'z') return (uint8_t)(value - 'a' + 'A');
    return value;
}

static uint64_t cluster_sector(const struct fat_volume *volume, uint32_t cluster) {
    return volume->data_start_sector +
           (uint64_t)(cluster - FAT_FIRST_DATA_CLUSTER) * volume->sectors_per_cluster;
}

static bool cluster_in_range(const struct fat_volume *volume, uint32_t cluster) {
    if (cluster < FAT_FIRST_DATA_CLUSTER) return false;
    return cluster < FAT_FIRST_DATA_CLUSTER + volume->cluster_count;
}

bool fat_mount(struct block_device *device, struct fat_volume *volume) {
    if (!block_device_valid(device) || !volume) return false;

    uint8_t sector[BLOCK_SECTOR_BYTES_DEFAULT];
    if (!block_read(device, 0, 1, sector)) return false;

    uint16_t signature = (uint16_t)(sector[FAT_BPB_SIGNATURE_OFFSET] |
                                    (sector[FAT_BPB_SIGNATURE_OFFSET + 1U] << 8));
    if (signature != FAT_BPB_SIGNATURE) return false;

    struct reader reader;
    reader_init(&reader, sector, sizeof sector);

    uint16_t bytes_per_sector, reserved_sectors, root_entry_count, total_16;
    uint16_t sectors_per_fat_16;
    uint8_t sectors_per_cluster, fat_count;
    uint32_t total_32, sectors_per_fat_32, root_cluster;

    if (!reader_seek(&reader, FAT_BPB_BYTES_PER_SECTOR_OFFSET)) return false;
    if (!reader_u16(&reader, &bytes_per_sector)) return false;
    if (!reader_u8(&reader, &sectors_per_cluster)) return false;
    if (!reader_u16(&reader, &reserved_sectors)) return false;
    if (!reader_u8(&reader, &fat_count)) return false;
    if (!reader_u16(&reader, &root_entry_count)) return false;
    if (!reader_u16(&reader, &total_16)) return false;
    if (!reader_seek(&reader, FAT_BPB_SECTORS_PER_FAT_16_OFFSET)) return false;
    if (!reader_u16(&reader, &sectors_per_fat_16)) return false;
    if (!reader_seek(&reader, FAT_BPB_TOTAL_SECTORS_32_OFFSET)) return false;
    if (!reader_u32(&reader, &total_32)) return false;
    if (!reader_u32(&reader, &sectors_per_fat_32)) return false;
    if (!reader_seek(&reader, FAT_BPB_ROOT_CLUSTER_OFFSET)) return false;
    if (!reader_u32(&reader, &root_cluster)) return false;

    /* FAT32 alone: the two 16-bit fields being zero is what distinguishes it,
       and a volume where they are not is a FAT12 or FAT16 this does not read. */
    if (root_entry_count != 0 || sectors_per_fat_16 != 0) return false;
    if (sectors_per_fat_32 == 0) return false;

    if (bytes_per_sector != device->sector_bytes) return false;
    if (sectors_per_cluster == 0) return false;
    /* A power of two, or the cluster arithmetic below stops being addressable. */
    if ((sectors_per_cluster & (sectors_per_cluster - 1U)) != 0) return false;
    if (reserved_sectors == 0 || fat_count == 0) return false;

    uint32_t total_sectors = total_32 != 0 ? total_32 : total_16;
    if (total_sectors == 0) return false;
    if (device->sector_count != 0 && total_sectors > device->sector_count)
        return false;

    uint64_t fat_sectors;
    if (!checked_mul_u64(fat_count, sectors_per_fat_32, &fat_sectors)) return false;

    uint64_t data_start;
    if (!checked_add_u64(reserved_sectors, fat_sectors, &data_start)) return false;
    if (data_start >= total_sectors) return false;

    uint64_t data_sectors = total_sectors - data_start;
    uint64_t clusters = data_sectors / sectors_per_cluster;
    if (clusters == 0) return false;

    uint64_t cluster_bytes;
    if (!checked_mul_u64(sectors_per_cluster, bytes_per_sector, &cluster_bytes))
        return false;

    volume->device = device;
    volume->bytes_per_sector = bytes_per_sector;
    volume->sectors_per_cluster = sectors_per_cluster;
    volume->cluster_bytes = (uint32_t)cluster_bytes;
    volume->fat_start_sector = reserved_sectors;
    volume->data_start_sector = data_start;
    volume->sectors_per_fat = sectors_per_fat_32;
    volume->root_cluster = root_cluster;
    volume->cluster_count =
        clusters > UINT32_MAX ? UINT32_MAX : (uint32_t)clusters;

    volume->fat_sector_valid = false;

    if (!cluster_in_range(volume, root_cluster)) return false;
    return true;
}

static bool next_cluster(struct fat_volume *volume, uint32_t cluster,
                         uint32_t *out) {
    if (!cluster_in_range(volume, cluster)) return false;

    uint64_t offset;
    if (!checked_mul_u64(cluster, FAT_FAT_ENTRY_BYTES, &offset)) return false;

    uint64_t limit;
    if (!checked_mul_u64(volume->sectors_per_fat, volume->bytes_per_sector,
                         &limit)) return false;
    if (offset + FAT_FAT_ENTRY_BYTES > limit) return false;

    uint64_t absolute;
    if (!checked_add_u64(volume->fat_start_sector * volume->bytes_per_sector,
                         offset, &absolute)) return false;

    uint64_t sector = absolute / volume->bytes_per_sector;
    if (!volume->fat_sector_valid || volume->fat_sector_index != sector) {
        if (!block_read(volume->device, sector, 1, volume->fat_sector))
            return false;
        volume->fat_sector_index = sector;
        volume->fat_sector_valid = true;
    }

    const uint8_t *raw =
        volume->fat_sector + (absolute % volume->bytes_per_sector);
    uint32_t value = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
                     ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    *out = value & FAT_CLUSTER_MASK;
    return true;
}

static bool is_end_of_chain(uint32_t cluster) {
    return cluster >= FAT_CLUSTER_END || cluster == FAT_CLUSTER_BAD;
}

/* Walks `count` links along the chain. Bounded by the number of clusters that
   exist, so a FAT that loops back on itself ends the walk instead of the boot. */
static bool advance(struct fat_volume *volume, uint32_t cluster, uint64_t count,
                    uint32_t *out) {
    uint64_t steps = 0;
    while (count > 0) {
        if (!next_cluster(volume, cluster, &cluster)) return false;
        if (is_end_of_chain(cluster)) return false;
        if (!cluster_in_range(volume, cluster)) return false;
        if (++steps > volume->cluster_count) return false;
        count--;
    }
    *out = cluster;
    return true;
}

/* Bytes from a cluster chain, which is what both files and directories are.
   `walk` may be null; when it is not, a read that starts at or after where the
   last one reached carries on from there instead of from the first cluster. */
static bool read_chain(struct fat_volume *volume, uint32_t first_cluster,
                       uint64_t offset, size_t length, void *out,
                       struct fat_walk *walk) {
    if (length == 0) return true;

    uint64_t target_index = offset / volume->cluster_bytes;
    uint32_t cluster = first_cluster;
    uint64_t index = 0;

    if (walk && walk->valid && walk->offset <= target_index) {
        cluster = walk->cluster;
        index = walk->offset;
    }
    if (!advance(volume, cluster, target_index - index, &cluster)) return false;

    if (walk) {
        walk->cluster = cluster;
        walk->offset = target_index;
        walk->valid = true;
    }

    uint8_t *cursor = (uint8_t *)out;
    uint32_t into_cluster = (uint32_t)(offset % volume->cluster_bytes);
    uint64_t walked = 0;

    while (length > 0) {
        if (!cluster_in_range(volume, cluster)) return false;

        uint32_t available = volume->cluster_bytes - into_cluster;
        size_t take = available < length ? available : length;

        uint64_t byte_offset;
        if (!checked_mul_u64(cluster_sector(volume, cluster),
                             volume->bytes_per_sector, &byte_offset)) return false;
        if (!checked_add_u64(byte_offset, into_cluster, &byte_offset)) return false;
        if (!block_read_bytes(volume->device, byte_offset, take, cursor))
            return false;

        cursor += take;
        length -= take;
        into_cluster = 0;
        if (length == 0) break;

        if (!next_cluster(volume, cluster, &cluster)) return false;
        if (is_end_of_chain(cluster)) return false;
        if (++walked > volume->cluster_count) return false;
    }
    return true;
}

/* "kernel.elf" against the padded "KERNEL  ELF" an entry actually holds. */
static bool name_matches(const uint8_t *entry_name, const char *name,
                         size_t length) {
    uint8_t wanted[FAT_ENTRY_NAME_BYTES];
    for (unsigned index = 0; index < FAT_ENTRY_NAME_BYTES; index++)
        wanted[index] = NAME_PADDING;

    size_t base = 0;
    while (base < length && name[base] != EXTENSION_SEPARATOR) base++;
    if (base > NAME_BASE_BYTES) return false;

    for (size_t index = 0; index < base; index++)
        wanted[index] = upper((uint8_t)name[index]);

    if (base < length) {
        size_t extension = length - base - 1U;
        if (extension > NAME_EXTENSION_BYTES) return false;
        for (size_t index = 0; index < extension; index++)
            wanted[NAME_BASE_BYTES + index] = upper((uint8_t)name[base + 1U + index]);
    }

    for (unsigned index = 0; index < FAT_ENTRY_NAME_BYTES; index++) {
        if (upper(entry_name[index]) != wanted[index]) return false;
    }
    return true;
}

/* Where the 13 characters of a long-name fragment sit inside its 32 bytes. */
static const uint8_t long_name_offsets[FAT_LONG_CHARS_PER_ENTRY] = {
    1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30,
};

struct long_name {
    char text[FAT_LONG_NAME_MAX];
    size_t length;
    uint8_t checksum;
    bool valid;
};

/* The one link between a long name and the 8.3 entry it belongs to. Without
   checking it, fragments left behind by a deleted file attach themselves to
   whatever entry happens to follow. */
static uint8_t short_name_checksum(const uint8_t *name) {
    uint8_t sum = 0;
    for (unsigned index = 0; index < FAT_ENTRY_NAME_BYTES; index++) {
        sum = (uint8_t)(((sum & 1U) << 7) + (sum >> 1) + name[index]);
    }
    return sum;
}

static void long_name_reset(struct long_name *pending) {
    pending->length = 0;
    pending->valid = false;
}

static void long_name_accumulate(struct long_name *pending, const uint8_t *entry) {
    uint8_t sequence = entry[FAT_LONG_SEQUENCE_OFFSET] & FAT_LONG_SEQUENCE_MASK;
    if (sequence == 0 || sequence > FAT_LONG_MAX_SEQUENCE) {
        long_name_reset(pending);
        return;
    }

    if (entry[FAT_LONG_SEQUENCE_OFFSET] & FAT_LONG_SEQUENCE_LAST) {
        long_name_reset(pending);
        pending->checksum = entry[FAT_LONG_CHECKSUM_OFFSET];
        pending->length = (size_t)sequence * FAT_LONG_CHARS_PER_ENTRY;
        pending->valid = true;
    }
    if (!pending->valid) return;
    if (entry[FAT_LONG_CHECKSUM_OFFSET] != pending->checksum) {
        long_name_reset(pending);
        return;
    }

    size_t base = (size_t)(sequence - 1U) * FAT_LONG_CHARS_PER_ENTRY;
    for (unsigned index = 0; index < FAT_LONG_CHARS_PER_ENTRY; index++) {
        uint8_t at = long_name_offsets[index];
        uint16_t unit = (uint16_t)(entry[at] | (entry[at + 1U] << 8));
        size_t position = base + index;
        if (position >= FAT_LONG_NAME_MAX) {
            long_name_reset(pending);
            return;
        }
        if (unit == 0x0000U || unit == 0xFFFFU) {
            /* The name ends here; anything after is padding. */
            if (position < pending->length) pending->length = position;
            continue;
        }
        /* Only ASCII can be compared against the paths this loader is given.
           A name with anything else falls back to its 8.3 form rather than
           being matched approximately. */
        if (unit > 0x7FU) {
            long_name_reset(pending);
            return;
        }
        pending->text[position] = (char)unit;
    }
}

static bool long_name_matches(const struct long_name *pending, const char *name,
                              size_t length) {
    if (!pending->valid || pending->length != length) return false;
    for (size_t index = 0; index < length; index++) {
        if (upper((uint8_t)pending->text[index]) != upper((uint8_t)name[index]))
            return false;
    }
    return true;
}

/* One directory, searched for one path component. */
static bool find_in_directory(struct fat_volume *volume, uint32_t cluster,
                              const char *name, size_t length,
                              struct fat_file *out) {
    uint8_t entry[FAT_DIRECTORY_ENTRY_BYTES];
    struct long_name pending;
    uint64_t offset = 0;
    uint64_t entries_seen = 0;

    long_name_reset(&pending);
    uint64_t maximum_entries =
        (uint64_t)volume->cluster_count * volume->cluster_bytes /
        FAT_DIRECTORY_ENTRY_BYTES;

    while (entries_seen++ < maximum_entries) {
        if (!read_chain(volume, cluster, offset, sizeof entry, entry, NULL))
            return false;
        offset += FAT_DIRECTORY_ENTRY_BYTES;

        uint8_t first = entry[FAT_ENTRY_NAME_OFFSET];
        if (first == FAT_ENTRY_END_OF_DIRECTORY) return false;
        if (first == FAT_ENTRY_DELETED) {
            long_name_reset(&pending);
            continue;
        }

        uint8_t attributes = entry[FAT_ENTRY_ATTRIBUTES_OFFSET];
        if (attributes == FAT_ATTRIBUTE_LONG_NAME) {
            long_name_accumulate(&pending, entry);
            continue;
        }

        const uint8_t *short_name = entry + FAT_ENTRY_NAME_OFFSET;
        bool long_belongs = pending.valid &&
                            pending.checksum == short_name_checksum(short_name);
        /* Either name reaches the file: giving something a long name does not
           take its 8.3 name away, and both are what a user may have written. */
        bool matched = (long_belongs && long_name_matches(&pending, name, length)) ||
                       name_matches(short_name, name, length);
        long_name_reset(&pending);

        if (attributes & FAT_ATTRIBUTE_VOLUME_LABEL) continue;
        if (!matched) continue;

        uint32_t high = (uint32_t)(entry[FAT_ENTRY_CLUSTER_HIGH_OFFSET] |
                                   (entry[FAT_ENTRY_CLUSTER_HIGH_OFFSET + 1U] << 8));
        uint32_t low = (uint32_t)(entry[FAT_ENTRY_CLUSTER_LOW_OFFSET] |
                                  (entry[FAT_ENTRY_CLUSTER_LOW_OFFSET + 1U] << 8));

        out->volume = volume;
        out->first_cluster = (high << 16) | low;
        out->size = (uint32_t)entry[FAT_ENTRY_SIZE_OFFSET] |
                    ((uint32_t)entry[FAT_ENTRY_SIZE_OFFSET + 1U] << 8) |
                    ((uint32_t)entry[FAT_ENTRY_SIZE_OFFSET + 2U] << 16) |
                    ((uint32_t)entry[FAT_ENTRY_SIZE_OFFSET + 3U] << 24);
        out->directory = (attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
        out->walk.valid = false;
        return true;
    }
    return false;
}

bool fat_open(struct fat_volume *volume, const char *path,
              struct fat_file *out) {
    if (!volume || !path || !out) return false;

    struct fat_file current;
    current.volume = volume;
    current.first_cluster = volume->root_cluster;
    current.size = 0;
    current.directory = true;
    current.walk.valid = false;

    size_t index = 0;
    while (path[index] == PATH_SEPARATOR) index++;

    while (path[index] != '\0') {
        if (index >= FAT_MAX_PATH_BYTES) return false;
        if (!current.directory) return false;

        size_t start = index;
        while (path[index] != '\0' && path[index] != PATH_SEPARATOR) index++;
        size_t length = index - start;
        if (length == 0 || length > FAT_MAX_NAME_BYTES) return false;

        if (!find_in_directory(volume, current.first_cluster, path + start,
                               length, &current)) {
            return false;
        }
        while (path[index] == PATH_SEPARATOR) index++;
    }

    /* An empty path is the root, which is a directory with no entry of its own
       and therefore no size to report. */
    *out = current;
    return true;
}

bool fat_read(struct fat_file *file, uint64_t offset, size_t length, void *out) {
    if (!file || !file->volume || !out) return false;
    if (file->directory) return false;
    if (length == 0) return true;

    uint64_t end;
    if (!checked_add_u64(offset, length, &end)) return false;
    if (end > file->size) return false;

    return read_chain(file->volume, file->first_cluster, offset, length, out,
                      &file->walk);
}
