#include "harness.h"
#include "fw/bios/e820.h"

#define MIB (1024ULL * 1024ULL)
#define GIB (1024ULL * MIB)
#define ENTRY_CAPACITY E820_MAX_ENTRIES

static uint8_t buffer[ENTRY_CAPACITY * E820_ENTRY_BYTES];
static uint32_t entries;
static struct memory_map map;

static void put_u64(uint8_t *at, uint64_t value) {
    for (unsigned index = 0; index < 8; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void put_u32(uint8_t *at, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void reset(void) {
    entries = 0;
    for (size_t index = 0; index < sizeof buffer; index++) buffer[index] = 0;
}

static void append(uint64_t base, uint64_t length, uint32_t type,
                   uint32_t attributes) {
    uint8_t *at = buffer + entries * E820_ENTRY_BYTES;
    put_u64(at, base);
    put_u64(at + 8, length);
    put_u32(at + 16, type);
    put_u32(at + 20, attributes);
    entries++;
}

/* What stage2 leaves behind on a 2 GiB QEMU machine, entry for entry. */
static void parses_a_real_firmware_reply(void) {
    reset();
    append(0x0, 0x9FC00, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(0x9FC00, 0x400, E820_TYPE_RESERVED, E820_ATTRIBUTE_VALID);
    append(0xF0000, 0x10000, E820_TYPE_RESERVED, E820_ATTRIBUTE_VALID);
    append(0x100000, 0x7FF00000, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(0x7FFF0000, 0x10000, E820_TYPE_ACPI_RECLAIMABLE, E820_ATTRIBUTE_VALID);
    append(0xFFFC0000, 0x40000, E820_TYPE_RESERVED, E820_ATTRIBUTE_VALID);
    append(0x100000000ULL, 0x0, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);

    CHECK(e820_parse(buffer, entries, &map));
    CHECK(!map.truncated);

    /* The zero-length last entry is dropped rather than becoming a region. */
    CHECK(memory_map_find(&map, 0x100000000ULL) == NULL);
    /* The ACPI entry sits inside the top of the big usable run, and firmware
       expects the tables there to survive us, so those bytes stop being usable. */
    CHECK(memory_map_find(&map, 0x7FFF8000)->kind == MEMORY_KIND_ACPI_RECLAIMABLE);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) ==
          0x9FC00 + 0x7FF00000 - 0x10000);
    CHECK(memory_map_total(&map, MEMORY_KIND_ACPI_RECLAIMABLE) == 0x10000);
}

static void an_entry_marked_invalid_is_ignored(void) {
    reset();
    append(0, 4 * MIB, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(8 * MIB, 4 * MIB, E820_TYPE_USABLE, 0);

    CHECK(e820_parse(buffer, entries, &map));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 4 * MIB);
    CHECK(memory_map_find(&map, 8 * MIB) == NULL);
}

static void an_unknown_type_is_treated_as_reserved(void) {
    reset();
    append(0, 4 * MIB, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(4 * MIB, 4 * MIB, 12345, E820_ATTRIBUTE_VALID);

    CHECK(e820_parse(buffer, entries, &map));
    /* Memory of a kind we have never heard of is not memory we hand out. */
    CHECK(memory_map_find(&map, 5 * MIB)->kind == MEMORY_KIND_RESERVED);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 4 * MIB);
}

static void every_defined_type_maps_somewhere(void) {
    CHECK(e820_kind(E820_TYPE_USABLE) == MEMORY_KIND_USABLE);
    CHECK(e820_kind(E820_TYPE_RESERVED) == MEMORY_KIND_RESERVED);
    CHECK(e820_kind(E820_TYPE_ACPI_RECLAIMABLE) == MEMORY_KIND_ACPI_RECLAIMABLE);
    CHECK(e820_kind(E820_TYPE_ACPI_NVS) == MEMORY_KIND_ACPI_NVS);
    CHECK(e820_kind(E820_TYPE_BAD) == MEMORY_KIND_BAD);
    CHECK(e820_kind(0) == MEMORY_KIND_RESERVED);
}

static void a_wrapping_entry_is_dropped_not_believed(void) {
    reset();
    append(0, 4 * MIB, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(UINT64_MAX - 0x1000, 0x8000, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);

    CHECK(e820_parse(buffer, entries, &map));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 4 * MIB);
}

static void one_bad_row_does_not_lose_the_rest(void) {
    reset();
    append(0, 0, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(MIB, 16 * MIB, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    append(0, 0, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);

    CHECK(e820_parse(buffer, entries, &map));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 16 * MIB);
}

static void nothing_usable_is_a_failure(void) {
    reset();
    CHECK(!e820_parse(buffer, 0, &map));
    CHECK(!e820_parse(NULL, 4, &map));
    CHECK(map.count == 0);

    append(0, 4 * MIB, E820_TYPE_RESERVED, E820_ATTRIBUTE_VALID);
    /* Parsing succeeded, but a loader with nowhere to put anything has not. */
    CHECK(!e820_parse(buffer, entries, &map));
    CHECK(map.count == 1);
}

static void a_count_beyond_the_maximum_is_capped(void) {
    reset();
    for (unsigned index = 0; index < E820_MAX_ENTRIES; index++) {
        append(index * 2 * MIB, MIB, E820_TYPE_USABLE, E820_ATTRIBUTE_VALID);
    }

    /* The count arrives from firmware and is used to index memory. Reading the
       entries it claims rather than the ones that exist is how a parser walks
       off the end of the buffer it was handed. */
    CHECK(e820_parse(buffer, E820_MAX_ENTRIES + 1000U, &map));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == E820_MAX_ENTRIES * MIB);
}

TEST_MAIN(
    parses_a_real_firmware_reply();
    an_entry_marked_invalid_is_ignored();
    an_unknown_type_is_treated_as_reserved();
    every_defined_type_maps_somewhere();
    a_wrapping_entry_is_dropped_not_believed();
    one_bad_row_does_not_lose_the_rest();
    nothing_usable_is_a_failure();
    a_count_beyond_the_maximum_is_capped();
)
