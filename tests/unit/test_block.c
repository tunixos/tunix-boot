#include "harness.h"
#include "block/block.h"
#include "util/mem.h"

#define DISK_SECTORS 64U
#define SECTOR_BYTES BLOCK_SECTOR_BYTES_DEFAULT
#define DISK_BYTES (DISK_SECTORS * SECTOR_BYTES)

static uint8_t disk[DISK_BYTES];
static unsigned read_calls;
static uint32_t largest_request;
static bool read_fails;

/* Every byte differs from its neighbours in both directions, so a read that is
   off by one sector or one byte cannot accidentally match. */
static uint8_t pattern(size_t index) {
    return (uint8_t)(index * 31U + (index >> 8) * 17U + 7U);
}

static bool fake_read(const struct block_device *device, uint64_t lba,
                      uint32_t count, void *out) {
    (void)device;
    read_calls++;
    if (count > largest_request) largest_request = count;
    if (read_fails) return false;
    CHECK(count > 0 && count <= BLOCK_MAX_SECTORS_PER_READ);
    CHECK(lba + count <= DISK_SECTORS);
    memcpy(out, disk + lba * SECTOR_BYTES, (size_t)count * SECTOR_BYTES);
    return true;
}

static struct block_device device;

static void setup(void) {
    for (size_t index = 0; index < DISK_BYTES; index++) disk[index] = pattern(index);
    read_calls = 0;
    largest_request = 0;
    read_fails = false;

    device.name = "fake";
    device.read_sectors = fake_read;
    device.sector_bytes = SECTOR_BYTES;
    device.sector_count = DISK_SECTORS;
    device.context = NULL;
}

static bool matches(const uint8_t *got, size_t offset, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (got[index] != pattern(offset + index)) return false;
    }
    return true;
}

static void rejects_a_device_it_cannot_use(void) {
    setup();
    CHECK(block_device_valid(&device));
    CHECK(!block_device_valid(NULL));

    struct block_device broken = device;
    broken.read_sectors = NULL;
    CHECK(!block_device_valid(&broken));

    broken = device;
    broken.sector_bytes = 0;
    CHECK(!block_device_valid(&broken));

    /* A sector bigger than the scratch buffer would overrun it on every
       unaligned read, so it is refused up front. */
    broken = device;
    broken.sector_bytes = sizeof broken.bounce + 1;
    CHECK(!block_device_valid(&broken));
}

static void reads_whole_sectors(void) {
    uint8_t got[4 * SECTOR_BYTES];
    setup();

    CHECK(block_read(&device, 2, 4, got));
    CHECK(matches(got, 2 * SECTOR_BYTES, sizeof got));
}

static void splits_a_request_the_backend_cannot_take(void) {
    static uint8_t got[DISK_BYTES];
    setup();

    CHECK(block_read(&device, 0, DISK_SECTORS, got));
    CHECK(largest_request <= BLOCK_MAX_SECTORS_PER_READ);
    CHECK(matches(got, 0, sizeof got));
}

static void refuses_to_read_past_the_end(void) {
    uint8_t got[SECTOR_BYTES];
    setup();

    CHECK(!block_read(&device, DISK_SECTORS, 1, got));
    CHECK(!block_read(&device, DISK_SECTORS - 1, 2, got));
    CHECK(!block_read(&device, UINT64_MAX, 2, got));
    CHECK(read_calls == 0);

    CHECK(block_read(&device, DISK_SECTORS - 1, 1, got));
}

static void a_device_of_unknown_size_is_not_second_guessed(void) {
    uint8_t got[SECTOR_BYTES];
    setup();
    device.sector_count = 0;

    /* Nothing here knows the size, so the backing service is the one that
       refuses; inventing a limit would fail reads that would have worked. */
    CHECK(block_read(&device, 8, 1, got));
    CHECK(matches(got, 8 * SECTOR_BYTES, sizeof got));
}

static void a_backend_failure_is_not_hidden(void) {
    uint8_t got[SECTOR_BYTES];
    setup();
    read_fails = true;

    CHECK(!block_read(&device, 0, 1, got));
    CHECK(!block_read_bytes(&device, 0, 16, got));
}

static void reads_a_range_inside_one_sector(void) {
    uint8_t got[16];
    setup();

    CHECK(block_read_bytes(&device, 100, sizeof got, got));
    CHECK(matches(got, 100, sizeof got));
    CHECK(read_calls == 1);
}

static void reads_a_range_that_straddles_a_sector_edge(void) {
    uint8_t got[32];
    setup();

    CHECK(block_read_bytes(&device, SECTOR_BYTES - 16, sizeof got, got));
    CHECK(matches(got, SECTOR_BYTES - 16, sizeof got));
}

static void reads_a_range_unaligned_at_both_ends(void) {
    static uint8_t got[3 * SECTOR_BYTES + 100];
    setup();

    const uint64_t offset = SECTOR_BYTES + 33;
    CHECK(block_read_bytes(&device, offset, sizeof got, got));
    CHECK(matches(got, offset, sizeof got));
}

static void reads_every_offset_and_length_near_a_sector_edge(void) {
    static uint8_t got[2 * SECTOR_BYTES + 8];
    setup();

    /* The ends are where this arithmetic goes wrong, so walk across two sector
       boundaries a byte at a time rather than trusting a few spot checks. */
    for (uint64_t offset = SECTOR_BYTES - 4; offset <= SECTOR_BYTES + 4; offset++) {
        for (size_t length = 1; length <= sizeof got; length += 61) {
            CHECK(block_read_bytes(&device, offset, length, got));
            CHECK(matches(got, offset, length));
        }
    }
}

static void an_exactly_aligned_range_skips_the_scratch_buffer(void) {
    uint8_t got[2 * SECTOR_BYTES];
    setup();

    CHECK(block_read_bytes(&device, SECTOR_BYTES, sizeof got, got));
    CHECK(matches(got, SECTOR_BYTES, sizeof got));
    /* Two sectors, one request, no bounce: the common case must not copy twice. */
    CHECK(read_calls == 1);
}

static void refuses_a_byte_range_past_the_end(void) {
    uint8_t got[SECTOR_BYTES];
    setup();

    CHECK(!block_read_bytes(&device, DISK_BYTES, 1, got));
    CHECK(!block_read_bytes(&device, DISK_BYTES - 4, 8, got));
    CHECK(!block_read_bytes(&device, UINT64_MAX - 2, 8, got));
    CHECK(read_calls == 0);

    /* The very last byte is still readable. */
    CHECK(block_read_bytes(&device, DISK_BYTES - 1, 1, got));
    CHECK(got[0] == pattern(DISK_BYTES - 1));
}

static void an_empty_read_asks_the_device_nothing(void) {
    uint8_t got[4] = {1, 2, 3, 4};
    setup();

    CHECK(block_read_bytes(&device, 100, 0, got));
    CHECK(read_calls == 0);
    CHECK(got[0] == 1);
}

TEST_MAIN(
    rejects_a_device_it_cannot_use();
    reads_whole_sectors();
    splits_a_request_the_backend_cannot_take();
    refuses_to_read_past_the_end();
    a_device_of_unknown_size_is_not_second_guessed();
    a_backend_failure_is_not_hidden();
    reads_a_range_inside_one_sector();
    reads_a_range_that_straddles_a_sector_edge();
    reads_a_range_unaligned_at_both_ends();
    reads_every_offset_and_length_near_a_sector_edge();
    an_exactly_aligned_range_skips_the_scratch_buffer();
    refuses_a_byte_range_past_the_end();
    an_empty_read_asks_the_device_nothing();
)
