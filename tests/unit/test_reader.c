#include "harness.h"
#include "util/reader.h"

static const uint8_t SAMPLE[] = {
    0x11,
    0x22, 0x33,
    0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
};

static void reads_little_endian_scalars(void) {
    struct reader reader;
    reader_init(&reader, SAMPLE, sizeof SAMPLE);

    uint8_t byte = 0;
    uint16_t half = 0;
    uint32_t word = 0;
    uint64_t giant = 0;

    CHECK(reader_u8(&reader, &byte) && byte == 0x11);
    CHECK(reader_u16(&reader, &half) && half == 0x3322);
    CHECK(reader_u32(&reader, &word) && word == 0x77665544);
    CHECK(reader_u64(&reader, &giant) && giant == 0xFFEEDDCCBBAA9988ULL);
    CHECK(reader_remaining(&reader) == 0);
}

static void refuses_to_read_past_the_end(void) {
    struct reader reader;
    uint32_t word = 0;
    reader_init(&reader, SAMPLE, 3);

    CHECK(!reader_u32(&reader, &word));
    /* A refused read must not have moved the cursor. */
    CHECK(reader_remaining(&reader) == 3);
}

static void refuses_a_length_that_would_wrap(void) {
    struct reader reader;
    reader_init(&reader, SAMPLE, sizeof SAMPLE);
    CHECK(!reader_skip(&reader, (size_t)-1));
    CHECK(reader_remaining(&reader) == sizeof SAMPLE);
}

static void take_borrows_without_copying(void) {
    struct reader reader;
    reader_init(&reader, SAMPLE, sizeof SAMPLE);

    const void *first = reader_take(&reader, 4);
    CHECK(first == SAMPLE);
    CHECK(reader_remaining(&reader) == sizeof SAMPLE - 4);
    CHECK(reader_take(&reader, sizeof SAMPLE) == NULL);
}

static void seek_and_peek_stay_in_range(void) {
    struct reader reader;
    uint8_t byte = 0;
    reader_init(&reader, SAMPLE, sizeof SAMPLE);

    CHECK(reader_seek(&reader, sizeof SAMPLE));
    CHECK(reader_remaining(&reader) == 0);
    CHECK(!reader_seek(&reader, sizeof SAMPLE + 1));
    CHECK(reader_seek(&reader, 1));
    CHECK(reader_peek_u8(&reader, 0, &byte) && byte == 0x22);
    CHECK(!reader_peek_u8(&reader, sizeof SAMPLE, &byte));
    /* Peeking does not consume. */
    CHECK(reader_remaining(&reader) == sizeof SAMPLE - 1);
}

static void an_empty_buffer_yields_nothing(void) {
    struct reader reader;
    uint8_t byte = 0;
    reader_init(&reader, NULL, 128);
    CHECK(reader_remaining(&reader) == 0);
    CHECK(!reader_u8(&reader, &byte));
}

TEST_MAIN(
    reads_little_endian_scalars();
    refuses_to_read_past_the_end();
    refuses_a_length_that_would_wrap();
    take_borrows_without_copying();
    seek_and_peek_stay_in_range();
    an_empty_buffer_yields_nothing();
)
