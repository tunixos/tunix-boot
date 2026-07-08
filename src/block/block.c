#include "block/block.h"
#include "util/checked.h"
#include "util/mem.h"

bool block_device_valid(const struct block_device *device) {
    if (!device || !device->read_sectors) return false;
    if (device->sector_bytes == 0) return false;
    /* A sector larger than the scratch buffer would make every unaligned read
       overrun it, so it is refused once here rather than checked everywhere. */
    if (device->sector_bytes > sizeof device->bounce) return false;
    return true;
}

static bool within_device(const struct block_device *device, uint64_t lba,
                          uint32_t count) {
    uint64_t end;
    if (!checked_add_u64(lba, count, &end)) return false;
    /* A device that will not report its size cannot contradict us; the backing
       service refuses the read instead. */
    if (device->sector_count == 0) return true;
    return end <= device->sector_count;
}

bool block_read(struct block_device *device, uint64_t lba, uint32_t count,
                void *out) {
    if (!block_device_valid(device) || !out || count == 0) return false;
    if (!within_device(device, lba, count)) return false;

    uint8_t *cursor = (uint8_t *)out;
    while (count > 0) {
        uint32_t chunk = count;
        if (chunk > BLOCK_MAX_SECTORS_PER_READ) chunk = BLOCK_MAX_SECTORS_PER_READ;
        if (!device->read_sectors(device, lba, chunk, cursor)) return false;

        cursor += (size_t)chunk * device->sector_bytes;
        lba += chunk;
        count -= chunk;
    }
    return true;
}

/* Part of one sector, copied through the scratch buffer. */
static bool read_partial_sector(struct block_device *device, uint64_t lba,
                                uint32_t start, uint32_t length, uint8_t *out) {
    if (!device->read_sectors(device, lba, 1, device->bounce)) return false;
    memcpy(out, device->bounce + start, length);
    return true;
}

bool block_read_bytes(struct block_device *device, uint64_t offset,
                      size_t length, void *out) {
    if (!block_device_valid(device) || !out) return false;
    if (length == 0) return true;

    uint64_t end;
    if (!checked_add_u64(offset, length, &end)) return false;

    const uint64_t sector_bytes = device->sector_bytes;
    uint8_t *cursor = (uint8_t *)out;
    uint64_t lba = offset / sector_bytes;
    uint32_t into_sector = (uint32_t)(offset % sector_bytes);

    if (device->sector_count != 0) {
        uint64_t last = (end - 1) / sector_bytes;
        if (last >= device->sector_count) return false;
    }

    if (into_sector != 0) {
        uint64_t available = sector_bytes - into_sector;
        uint32_t take = (uint32_t)(available < length ? available : length);
        if (!read_partial_sector(device, lba, into_sector, take, cursor))
            return false;
        cursor += take;
        length -= take;
        lba++;
    }

    /* Whole sectors go straight to the caller: this is the common case and the
       one worth not copying twice. */
    if (length >= sector_bytes) {
        uint64_t whole = length / sector_bytes;
        while (whole > 0) {
            uint32_t chunk = whole > BLOCK_MAX_SECTORS_PER_READ
                                 ? BLOCK_MAX_SECTORS_PER_READ
                                 : (uint32_t)whole;
            if (!device->read_sectors(device, lba, chunk, cursor)) return false;
            size_t moved = (size_t)chunk * sector_bytes;
            cursor += moved;
            length -= moved;
            lba += chunk;
            whole -= chunk;
        }
    }

    if (length > 0) {
        if (!read_partial_sector(device, lba, 0, (uint32_t)length, cursor))
            return false;
    }
    return true;
}
