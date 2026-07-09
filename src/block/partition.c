#include "block/partition.h"
#include "util/checked.h"
#include "util/mem.h"
#include "util/reader.h"

#define PARTITION_NAME "part"

static bool add_entry(struct partition_table *table, uint64_t start,
                      uint64_t sectors, uint8_t type, bool active,
                      uint64_t device_sectors) {
    uint64_t end;
    if (sectors == 0) return false;
    if (!checked_add_u64(start, sectors, &end)) return false;
    /* A partition claiming to extend past the disk is the one thing here that
       must never be believed: everything above computes offsets from it. */
    if (device_sectors != 0 && end > device_sectors) return false;
    if (table->count == PARTITION_MAX) return false;

    table->entries[table->count].start_lba = start;
    table->entries[table->count].sector_count = sectors;
    table->entries[table->count].type = type;
    table->entries[table->count].active = active;
    table->count++;
    return true;
}

bool partition_parse_mbr(const uint8_t *sector, uint64_t device_sectors,
                         struct partition_table *out) {
    out->count = 0;
    out->gpt = false;
    if (!sector) return false;

    uint16_t signature = (uint16_t)(sector[MBR_SIGNATURE_OFFSET] |
                                    (sector[MBR_SIGNATURE_OFFSET + 1U] << 8));
    if (signature != MBR_SIGNATURE) return false;

    for (unsigned index = 0; index < MBR_ENTRY_COUNT; index++) {
        struct reader reader;
        reader_init(&reader, sector + MBR_TABLE_OFFSET + index * MBR_ENTRY_BYTES,
                    MBR_ENTRY_BYTES);

        /* Seeking to each named offset rather than counting past the CHS
           fields, which nothing here uses and which no longer describe the
           geometry of any disk this will run on. */
        uint8_t status, type;
        uint32_t start, sectors;
        if (!reader_seek(&reader, MBR_ENTRY_STATUS_OFFSET)) break;
        if (!reader_u8(&reader, &status)) break;
        if (!reader_seek(&reader, MBR_ENTRY_TYPE_OFFSET)) break;
        if (!reader_u8(&reader, &type)) break;
        if (!reader_seek(&reader, MBR_ENTRY_START_LBA_OFFSET)) break;
        if (!reader_u32(&reader, &start)) break;
        if (!reader_seek(&reader, MBR_ENTRY_SECTORS_OFFSET)) break;
        if (!reader_u32(&reader, &sectors)) break;

        if (type == MBR_TYPE_EMPTY) continue;
        if (type == MBR_TYPE_GPT_PROTECTIVE) {
            out->gpt = true;
            out->count = 0;
            return true;
        }
        add_entry(out, start, sectors, type,
                  (status & MBR_STATUS_ACTIVE) != 0, device_sectors);
    }
    return true;
}

static bool entry_is_used(const uint8_t *type_guid) {
    for (unsigned index = 0; index < 16U; index++) {
        if (type_guid[index] != 0) return true;
    }
    return false;
}

static bool parse_gpt(struct block_device *device, struct partition_table *out) {
    uint8_t header[BLOCK_SECTOR_BYTES_DEFAULT];
    if (!block_read(device, GPT_HEADER_LBA, 1, header)) return false;

    if (memcmp(header, GPT_SIGNATURE, GPT_SIGNATURE_BYTES) != 0) return false;

    struct reader reader;
    reader_init(&reader, header, sizeof header);

    uint64_t entry_lba;
    uint32_t entry_count, entry_bytes;
    if (!reader_seek(&reader, GPT_HEADER_ENTRY_LBA_OFFSET)) return false;
    if (!reader_u64(&reader, &entry_lba)) return false;
    if (!reader_u32(&reader, &entry_count)) return false;
    if (!reader_u32(&reader, &entry_bytes)) return false;

    /* Both come from the disk and both size a loop that reads memory. */
    if (entry_bytes < GPT_ENTRY_MIN_BYTES || entry_bytes > GPT_ENTRY_MAX_BYTES)
        return false;
    if (entry_count > PARTITION_MAX) entry_count = PARTITION_MAX;

    uint8_t entry[GPT_ENTRY_MAX_BYTES];
    for (uint32_t index = 0; index < entry_count; index++) {
        uint64_t offset;
        if (!checked_mul_u64(index, entry_bytes, &offset)) break;
        if (!checked_add_u64(entry_lba * BLOCK_SECTOR_BYTES_DEFAULT, offset,
                             &offset)) break;
        if (!block_read_bytes(device, offset, entry_bytes, entry)) break;

        if (!entry_is_used(entry + GPT_ENTRY_TYPE_OFFSET)) continue;

        struct reader entry_reader;
        reader_init(&entry_reader, entry, entry_bytes);
        uint64_t first, last;
        if (!reader_seek(&entry_reader, GPT_ENTRY_FIRST_LBA_OFFSET)) break;
        if (!reader_u64(&entry_reader, &first)) break;
        if (!reader_u64(&entry_reader, &last)) break;
        if (last < first) continue;

        add_entry(out, first, last - first + 1U, 0, false, device->sector_count);
    }
    return out->count > 0;
}

bool partition_table_read(struct block_device *device,
                          struct partition_table *out) {
    out->count = 0;
    out->gpt = false;
    if (!block_device_valid(device)) return false;

    uint8_t sector[BLOCK_SECTOR_BYTES_DEFAULT];
    if (!block_read(device, 0, 1, sector)) return false;
    if (!partition_parse_mbr(sector, device->sector_count, out)) return false;

    if (out->gpt) return parse_gpt(device, out);
    return out->count > 0;
}

static bool partition_read_sectors(const struct block_device *device,
                                   uint64_t lba, uint32_t count, void *out) {
    const struct partition_view *view = (const struct partition_view *)device->context;
    if (!view || !view->parent) return false;

    uint64_t end;
    if (!checked_add_u64(lba, count, &end)) return false;
    if (end > device->sector_count) return false;

    uint64_t absolute;
    if (!checked_add_u64(view->start_lba, lba, &absolute)) return false;
    return view->parent->read_sectors(view->parent, absolute, count, out);
}

bool partition_open(struct block_device *device, const struct partition *entry,
                    struct partition_view *view, struct block_device *out) {
    if (!block_device_valid(device) || !entry || !view || !out) return false;
    if (entry->sector_count == 0) return false;

    uint64_t end;
    if (!checked_add_u64(entry->start_lba, entry->sector_count, &end)) return false;
    if (device->sector_count != 0 && end > device->sector_count) return false;

    view->parent = device;
    view->start_lba = entry->start_lba;

    out->name = PARTITION_NAME;
    out->read_sectors = partition_read_sectors;
    out->sector_bytes = device->sector_bytes;
    out->sector_count = entry->sector_count;
    out->context = view;
    return true;
}
