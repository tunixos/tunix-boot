#include "harness.h"
#include "block/partition.h"
#include "util/mem.h"

#define DISK_SECTORS 100000U
#define SECTOR_BYTES BLOCK_SECTOR_BYTES_DEFAULT

static uint8_t sector[SECTOR_BYTES];
static struct partition_table table;

static void clear_sector(void) {
    memset(sector, 0, sizeof sector);
    sector[MBR_SIGNATURE_OFFSET] = (uint8_t)(MBR_SIGNATURE & 0xFFU);
    sector[MBR_SIGNATURE_OFFSET + 1U] = (uint8_t)(MBR_SIGNATURE >> 8);
}

static void put_u32(uint8_t *at, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void set_entry(unsigned slot, uint8_t status, uint8_t type,
                      uint32_t start, uint32_t sectors) {
    uint8_t *at = sector + MBR_TABLE_OFFSET + slot * MBR_ENTRY_BYTES;
    at[MBR_ENTRY_STATUS_OFFSET] = status;
    at[MBR_ENTRY_TYPE_OFFSET] = type;
    put_u32(at + MBR_ENTRY_START_LBA_OFFSET, start);
    put_u32(at + MBR_ENTRY_SECTORS_OFFSET, sectors);
}

static void a_sector_without_the_signature_is_not_a_table(void) {
    clear_sector();
    set_entry(0, 0, 0x83U, 2048, 4096);
    sector[MBR_SIGNATURE_OFFSET] = 0;

    CHECK(!partition_parse_mbr(sector, DISK_SECTORS, &table));
    CHECK(table.count == 0);
    CHECK(!partition_parse_mbr(NULL, DISK_SECTORS, &table));
}

static void reads_the_entries_that_are_there(void) {
    clear_sector();
    set_entry(0, MBR_STATUS_ACTIVE, 0x0CU, 2048, 20480);
    set_entry(2, 0, 0x83U, 22528, 40960);

    CHECK(partition_parse_mbr(sector, DISK_SECTORS, &table));
    CHECK(!table.gpt);
    CHECK(table.count == 2);

    CHECK(table.entries[0].start_lba == 2048);
    CHECK(table.entries[0].sector_count == 20480);
    CHECK(table.entries[0].type == 0x0CU);
    CHECK(table.entries[0].active);

    /* The empty slot between them is skipped, not carried through as a hole. */
    CHECK(table.entries[1].start_lba == 22528);
    CHECK(!table.entries[1].active);
}

static void an_entry_reaching_past_the_disk_is_dropped(void) {
    clear_sector();
    set_entry(0, 0, 0x83U, 2048, 4096);
    set_entry(1, 0, 0x83U, DISK_SECTORS - 10U, 1000);

    /* Believing this would let every read through it land somewhere else. */
    CHECK(partition_parse_mbr(sector, DISK_SECTORS, &table));
    CHECK(table.count == 1);
    CHECK(table.entries[0].start_lba == 2048);
}

static void a_zero_length_entry_is_dropped(void) {
    clear_sector();
    set_entry(0, 0, 0x83U, 2048, 0);

    CHECK(partition_parse_mbr(sector, DISK_SECTORS, &table));
    CHECK(table.count == 0);
}

static void a_protective_entry_means_the_real_table_is_a_gpt(void) {
    clear_sector();
    set_entry(0, 0, MBR_TYPE_GPT_PROTECTIVE, 1, DISK_SECTORS - 1U);
    set_entry(1, 0, 0x83U, 2048, 4096);

    CHECK(partition_parse_mbr(sector, DISK_SECTORS, &table));
    CHECK(table.gpt);
    /* Whatever else the disguise sector says is not a partition table. */
    CHECK(table.count == 0);
}

static void an_unknown_disk_size_is_not_second_guessed(void) {
    clear_sector();
    set_entry(0, 0, 0x83U, 2048, 0xFFFFFFF0U);

    CHECK(partition_parse_mbr(sector, 0, &table));
    CHECK(table.count == 1);
}

/* A view over a fake parent, to prove reads land where the partition is. */
static uint8_t backing[64 * SECTOR_BYTES];
static uint64_t last_parent_lba;

static bool parent_read(const struct block_device *device, uint64_t lba,
                        uint32_t count, void *out) {
    (void)device;
    last_parent_lba = lba;
    if (lba + count > 64U) return false;
    memcpy(out, backing + lba * SECTOR_BYTES, (size_t)count * SECTOR_BYTES);
    return true;
}

static struct block_device parent;
static struct partition_view view;
static struct block_device confined;

static void setup_parent(void) {
    for (size_t index = 0; index < sizeof backing; index++)
        backing[index] = (uint8_t)(index * 7U + 1U);
    parent.name = "parent";
    parent.read_sectors = parent_read;
    parent.sector_bytes = SECTOR_BYTES;
    parent.sector_count = 64;
    parent.context = NULL;
    last_parent_lba = 0;
}

static void a_partition_addresses_from_zero(void) {
    struct partition entry = {.start_lba = 10, .sector_count = 8};
    uint8_t got[SECTOR_BYTES];
    setup_parent();

    CHECK(partition_open(&parent, &entry, &view, &confined));
    CHECK(confined.sector_count == 8);

    CHECK(block_read(&confined, 0, 1, got));
    CHECK(last_parent_lba == 10);
    CHECK(got[0] == backing[10 * SECTOR_BYTES]);

    CHECK(block_read(&confined, 3, 1, got));
    CHECK(last_parent_lba == 13);
    CHECK(got[0] == backing[13 * SECTOR_BYTES]);
}

static void a_partition_cannot_be_read_past(void) {
    struct partition entry = {.start_lba = 10, .sector_count = 8};
    uint8_t got[2 * SECTOR_BYTES];
    setup_parent();

    CHECK(partition_open(&parent, &entry, &view, &confined));

    /* Sector 8 exists on the disk but not in this partition, and a filesystem
       that overruns its own end must not reach the next one. */
    CHECK(!block_read(&confined, 8, 1, got));
    CHECK(!block_read(&confined, 7, 2, got));
    CHECK(block_read(&confined, 7, 1, got));
}

static void a_partition_outside_its_disk_does_not_open(void) {
    struct partition too_far = {.start_lba = 60, .sector_count = 8};
    struct partition empty = {.start_lba = 0, .sector_count = 0};
    setup_parent();

    CHECK(!partition_open(&parent, &too_far, &view, &confined));
    CHECK(!partition_open(&parent, &empty, &view, &confined));
    CHECK(!partition_open(&parent, NULL, &view, &confined));
}

TEST_MAIN(
    a_sector_without_the_signature_is_not_a_table();
    reads_the_entries_that_are_there();
    an_entry_reaching_past_the_disk_is_dropped();
    a_zero_length_entry_is_dropped();
    a_protective_entry_means_the_real_table_is_a_gpt();
    an_unknown_disk_size_is_not_second_guessed();
    a_partition_addresses_from_zero();
    a_partition_cannot_be_read_past();
    a_partition_outside_its_disk_does_not_open();
)
