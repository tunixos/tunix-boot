#include "harness.h"
#include "fw/uefi/memory.h"
#include "util/mem.h"

#define MIB (1024ULL * 1024ULL)
#define GIB (1024ULL * MIB)
#define ENTRY_CAPACITY 32U
/* Deliberately larger than the fields the specification defines, because real
   firmware reports a larger one and this is the bug that hides in that. */
#define FIRMWARE_DESCRIPTOR_BYTES 48U

static uint8_t descriptors[ENTRY_CAPACITY * FIRMWARE_DESCRIPTOR_BYTES];
static size_t descriptor_bytes;
static size_t used;
static struct memory_map map;

static void put_u32(uint8_t *at, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void put_u64(uint8_t *at, uint64_t value) {
    for (unsigned index = 0; index < 8; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void reset(size_t stride) {
    memset(descriptors, 0, sizeof descriptors);
    descriptor_bytes = stride;
    used = 0;
}

static void append(uint32_t type, uint64_t physical, uint64_t pages) {
    uint8_t *at = descriptors + used;
    put_u32(at + UEFI_DESCRIPTOR_TYPE_OFFSET, type);
    put_u64(at + UEFI_DESCRIPTOR_PHYSICAL_OFFSET, physical);
    put_u64(at + UEFI_DESCRIPTOR_VIRTUAL_OFFSET, physical);
    put_u64(at + UEFI_DESCRIPTOR_PAGES_OFFSET, pages);
    put_u64(at + UEFI_DESCRIPTOR_ATTRIBUTE_OFFSET, 0xF);
    /* The padding between the defined fields and the reported size is filled
       with something that is not zero, so a stride bug reads it as a type. */
    for (size_t index = UEFI_DESCRIPTOR_MIN_BYTES; index < descriptor_bytes;
         index++) {
        at[index] = 0xA5;
    }
    used += descriptor_bytes;
}

static uint64_t pages_of(uint64_t bytes) {
    return bytes / UEFI_PAGE_BYTES;
}

static bool parse(enum uefi_phase phase) {
    return uefi_memory_parse(descriptors, used, descriptor_bytes, phase, &map);
}

/* What OVMF hands over on a 2 GiB machine, shaped as it really comes. */
static void parses_a_firmware_map(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, 0, pages_of(640 * 1024));
    append(UEFI_MEMORY_RESERVED, 0xA0000, pages_of(0x60000));
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(1024 * MIB));
    append(UEFI_MEMORY_BOOT_SERVICES_DATA, 1025 * MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_LOADER_DATA, 1041 * MIB, pages_of(4 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 1045 * MIB, pages_of(1000 * MIB));
    append(UEFI_MEMORY_ACPI_RECLAIM, 2045 * MIB, pages_of(MIB));
    append(UEFI_MEMORY_ACPI_NVS, 2046 * MIB, pages_of(MIB));
    append(UEFI_MEMORY_RUNTIME_SERVICES_DATA, 2047 * MIB, pages_of(MIB));

    CHECK(parse(UEFI_BEFORE_EXIT));
    CHECK(!map.truncated);

    CHECK(memory_map_find(&map, 0)->kind == MEMORY_KIND_USABLE);
    CHECK(memory_map_find(&map, 0xA0000)->kind == MEMORY_KIND_RESERVED);
    CHECK(memory_map_find(&map, 2045 * MIB)->kind ==
          MEMORY_KIND_ACPI_RECLAIMABLE);
    CHECK(memory_map_find(&map, 2047 * MIB)->kind == MEMORY_KIND_RESERVED);

    /* The loader's own memory is reclaimable, not usable: the kernel may take
       it, but only once it has finished with what is in it. */
    CHECK(memory_map_find(&map, 1041 * MIB)->kind ==
          MEMORY_KIND_BOOTLOADER_RECLAIMABLE);
    CHECK(memory_map_total(&map, MEMORY_KIND_BOOTLOADER_RECLAIMABLE) == 4 * MIB);
}

/* The one that decides how much memory the machine appears to have. */
static void boot_services_memory_is_free_only_after_the_exit(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(64 * MIB));
    append(UEFI_MEMORY_BOOT_SERVICES_CODE, 65 * MIB, pages_of(8 * MIB));
    append(UEFI_MEMORY_BOOT_SERVICES_DATA, 73 * MIB, pages_of(8 * MIB));

    /* Before: the firmware is still running out of it. Writing there is
       overwriting the code that is about to be asked to exit. */
    CHECK(parse(UEFI_BEFORE_EXIT));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 64 * MIB);
    CHECK(memory_map_find(&map, 65 * MIB)->kind == MEMORY_KIND_RESERVED);

    /* After: it is ordinary free memory, and treating it as reserved would
       throw away sixteen megabytes for no reason. */
    CHECK(parse(UEFI_AFTER_EXIT));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 80 * MIB);
    CHECK(memory_map_find(&map, 65 * MIB)->kind == MEMORY_KIND_USABLE);
}

/* The bug this file exists to prevent. */
static void walks_by_the_reported_stride_not_the_struct_size(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 32 * MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 64 * MIB, pages_of(16 * MIB));

    CHECK(parse(UEFI_AFTER_EXIT));
    /* Stepping by the size of the defined fields would land in the padding and
       read 0xA5A5A5A5 as a memory type from the second entry onwards. */
    CHECK(map.count == 3);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 48 * MIB);
    CHECK(memory_map_find(&map, 64 * MIB)->kind == MEMORY_KIND_USABLE);
}

static void the_minimum_stride_also_works(void) {
    reset(UEFI_DESCRIPTOR_MIN_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 32 * MIB, pages_of(16 * MIB));

    CHECK(parse(UEFI_AFTER_EXIT));
    CHECK(map.count == 2);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 32 * MIB);
}

static void a_stride_that_makes_no_sense_is_refused(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));

    /* Smaller than the fields it must contain, so every read would run into
       the next descriptor. */
    CHECK(!uefi_memory_parse(descriptors, used, UEFI_DESCRIPTOR_MIN_BYTES - 8U,
                             UEFI_AFTER_EXIT, &map));
    CHECK(!uefi_memory_parse(descriptors, used, 0, UEFI_AFTER_EXIT, &map));
    CHECK(!uefi_memory_parse(descriptors, used, UEFI_DESCRIPTOR_MAX_BYTES + 1U,
                             UEFI_AFTER_EXIT, &map));
    CHECK(map.count == 0);
}

static void a_trailing_partial_descriptor_is_not_read(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 32 * MIB, pages_of(16 * MIB));

    /* The firmware's total need not be a whole number of descriptors, and the
       remainder is not one. */
    CHECK(uefi_memory_parse(descriptors, used - 8U, descriptor_bytes,
                            UEFI_AFTER_EXIT, &map));
    CHECK(map.count == 1);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 16 * MIB);
}

static void a_page_count_that_wraps_is_dropped(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));
    append(UEFI_MEMORY_CONVENTIONAL, 32 * MIB, UINT64_MAX / 2U);

    CHECK(parse(UEFI_AFTER_EXIT));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 16 * MIB);
}

static void a_zero_page_descriptor_is_dropped(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, 0, 0);
    append(UEFI_MEMORY_CONVENTIONAL, MIB, pages_of(16 * MIB));

    CHECK(parse(UEFI_AFTER_EXIT));
    CHECK(map.count == 1);
}

static void nothing_usable_is_a_failure(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    CHECK(!uefi_memory_parse(descriptors, 0, descriptor_bytes, UEFI_AFTER_EXIT,
                             &map));
    CHECK(!uefi_memory_parse(NULL, 128, descriptor_bytes, UEFI_AFTER_EXIT, &map));

    append(UEFI_MEMORY_RUNTIME_SERVICES_DATA, MIB, pages_of(MIB));
    /* Parsed successfully, but a loader with nowhere to work has not. */
    CHECK(!parse(UEFI_AFTER_EXIT));
    CHECK(map.count == 1);
}

static void an_unknown_type_is_treated_as_reserved(void) {
    CHECK(uefi_memory_kind(12345, UEFI_AFTER_EXIT) == MEMORY_KIND_RESERVED);
    CHECK(uefi_memory_kind(UEFI_MEMORY_PERSISTENT, UEFI_AFTER_EXIT) ==
          MEMORY_KIND_RESERVED);
    /* Memory of a kind we have never heard of is not memory we hand out. */
    CHECK(uefi_memory_kind(UEFI_MEMORY_MAPPED_IO, UEFI_AFTER_EXIT) ==
          MEMORY_KIND_RESERVED);
    CHECK(uefi_memory_kind(UEFI_MEMORY_UNUSABLE, UEFI_AFTER_EXIT) ==
          MEMORY_KIND_BAD);
}

static void overlapping_descriptors_resolve_the_same_way_as_e820(void) {
    reset(FIRMWARE_DESCRIPTOR_BYTES);
    append(UEFI_MEMORY_CONVENTIONAL, 0, pages_of(64 * MIB));
    append(UEFI_MEMORY_ACPI_NVS, 32 * MIB, pages_of(4 * MIB));

    CHECK(parse(UEFI_AFTER_EXIT));
    CHECK(memory_map_find(&map, 33 * MIB)->kind == MEMORY_KIND_ACPI_NVS);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 60 * MIB);
}

TEST_MAIN(
    parses_a_firmware_map();
    boot_services_memory_is_free_only_after_the_exit();
    walks_by_the_reported_stride_not_the_struct_size();
    the_minimum_stride_also_works();
    a_stride_that_makes_no_sense_is_refused();
    a_trailing_partial_descriptor_is_not_read();
    a_page_count_that_wraps_is_dropped();
    a_zero_page_descriptor_is_dropped();
    nothing_usable_is_a_failure();
    an_unknown_type_is_treated_as_reserved();
    overlapping_descriptors_resolve_the_same_way_as_e820();
)
