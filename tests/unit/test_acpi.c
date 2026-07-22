#include "harness.h"
#include "acpi/acpi.h"
#include "util/mem.h"

#define REGION_BYTES 4096U
#define TABLE_BYTES 1024U

/* The region an RSDP is scanned out of, as the low megabyte would be. */
static uint8_t region[REGION_BYTES] __attribute__((aligned(16)));
/* All four tables in one buffer, so a "physical address" is an offset into it.
   That is what acpi_map_fn exists for: the RSDT names tables with 32-bit
   entries, and a host pointer does not fit in one. */
static uint8_t pool[5 * TABLE_BYTES] __attribute__((aligned(8)));

/* Nothing at offset zero: physical address zero is never a table, and the
   mapper refuses it. */
#define RSDT_AT (1 * TABLE_BYTES)
#define XSDT_AT (2 * TABLE_BYTES)
#define MADT_AT (3 * TABLE_BYTES)
#define OTHER_AT (4 * TABLE_BYTES)

#define rsdt_storage (pool + RSDT_AT)
#define xsdt_storage (pool + XSDT_AT)
#define madt_storage (pool + MADT_AT)
#define other_storage (pool + OTHER_AT)

static const void *map_table(uint64_t physical, void *context) {
    (void)context;
    if (physical == 0 || physical >= sizeof pool) return NULL;
    return pool + physical;
}

static struct acpi_tables tables;

static void put_u32(uint8_t *at, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static void put_u64(uint8_t *at, uint64_t value) {
    for (unsigned index = 0; index < 8; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

/* Sets the byte at `offset` so the first `length` bytes sum to zero. */
static void fix_checksum(uint8_t *table, size_t length, size_t offset) {
    table[offset] = 0;
    uint8_t sum = 0;
    for (size_t index = 0; index < length; index++)
        sum = (uint8_t)(sum + table[index]);
    table[offset] = (uint8_t)(0u - sum);
}

static void begin_table(uint8_t *storage, const char *signature,
                        uint32_t length) {
    memset(storage, 0, TABLE_BYTES);
    memcpy(storage, signature, 4);
    put_u32(storage + ACPI_SDT_LENGTH_OFFSET, length);
}

static void finish_table(uint8_t *storage) {
    uint8_t *at = storage + ACPI_SDT_LENGTH_OFFSET;
    uint32_t length = (uint32_t)at[0] | ((uint32_t)at[1] << 8) |
                      ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
    fix_checksum(storage, length, 9);
}

/* An RSDP at `offset` in the region, pointing at both roots. */
static uint8_t *place_rsdp(size_t offset, uint8_t revision, const void *rsdt,
                           const void *xsdt) {
    uint8_t *at = region + offset;
    memcpy(at, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIGNATURE_BYTES);
    at[ACPI_RSDP_REVISION_OFFSET] = revision;
    put_u32(at + ACPI_RSDP_RSDT_OFFSET, (uint32_t)(uintptr_t)((const uint8_t *)rsdt - pool));

    if (revision >= ACPI_RSDP_REVISION_2) {
        put_u32(at + ACPI_RSDP_LENGTH_OFFSET, 36);
        put_u64(at + ACPI_RSDP_XSDT_OFFSET, (uint64_t)(uintptr_t)((const uint8_t *)xsdt - pool));
    }

    fix_checksum(at, ACPI_RSDP_REVISION_1_BYTES, ACPI_RSDP_CHECKSUM_OFFSET);
    if (revision >= ACPI_RSDP_REVISION_2) fix_checksum(at, 36, 32);
    return at;
}

/* A root table listing `count` pointers, wide or narrow. */
static void build_root(uint8_t *storage, const char *signature, bool wide,
                       const void **pointers, unsigned count) {
    uint32_t stride = wide ? 8U : 4U;
    uint32_t length = ACPI_SDT_HEADER_BYTES + count * stride;
    begin_table(storage, signature, length);

    for (unsigned index = 0; index < count; index++) {
        uint8_t *slot = storage + ACPI_SDT_HEADER_BYTES + index * stride;
        if (wide) {
            put_u64(slot, (uint64_t)(uintptr_t)((const uint8_t *)pointers[index] - pool));
        } else {
            put_u32(slot, (uint32_t)(uintptr_t)((const uint8_t *)pointers[index] - pool));
        }
    }
    finish_table(storage);
}

static void reset(void) {
    memset(region, 0, sizeof region);
    begin_table(other_storage, ACPI_SIGNATURE_FADT, 128);
    finish_table(other_storage);
}

static void a_checksum_is_the_bytes_adding_to_zero(void) {
    uint8_t bytes[4] = {0x10, 0x20, 0x30, 0xA0};
    CHECK(acpi_checksum(bytes, sizeof bytes));
    bytes[0] = 0x11;
    CHECK(!acpi_checksum(bytes, sizeof bytes));
    CHECK(acpi_checksum(bytes, 0));
}

static void recognises_an_rsdp(void) {
    reset();
    const uint8_t *rsdp = place_rsdp(64, 0, other_storage, NULL);
    CHECK(acpi_rsdp_valid(rsdp));
    CHECK(!acpi_rsdp_valid(NULL));
}

static void a_bad_checksum_is_not_an_rsdp(void) {
    reset();
    uint8_t *rsdp = place_rsdp(64, 0, other_storage, NULL);
    rsdp[ACPI_RSDP_CHECKSUM_OFFSET] ^= 0xFF;
    /* The one thing standing between a mis-scanned address and a walk through
       whatever is at it. */
    CHECK(!acpi_rsdp_valid(rsdp));
}

static void a_revision_two_rsdp_needs_both_checksums(void) {
    reset();
    uint8_t *rsdp = place_rsdp(64, 2, other_storage, xsdt_storage);
    CHECK(acpi_rsdp_valid(rsdp));

    /* The extended checksum covers the whole structure, and the first twenty
       bytes still add to zero without it. */
    rsdp[32] ^= 0xFF;
    CHECK(!acpi_rsdp_valid(rsdp));
}

static void a_nonsense_length_is_refused(void) {
    reset();
    uint8_t *rsdp = place_rsdp(64, 2, other_storage, xsdt_storage);
    put_u32(rsdp + ACPI_RSDP_LENGTH_OFFSET, 1000000);
    /* Believing it would sum a megabyte of unrelated memory. */
    CHECK(!acpi_rsdp_valid(rsdp));

    put_u32(rsdp + ACPI_RSDP_LENGTH_OFFSET, 8);
    CHECK(!acpi_rsdp_valid(rsdp));
}

static void finds_an_rsdp_in_a_region(void) {
    reset();
    const uint8_t *rsdp = place_rsdp(2048, 0, other_storage, NULL);
    CHECK(acpi_find_rsdp(region, sizeof region) == rsdp);

    reset();
    CHECK(acpi_find_rsdp(region, sizeof region) == NULL);
    CHECK(acpi_find_rsdp(NULL, sizeof region) == NULL);
}

static void an_rsdp_off_the_alignment_is_not_found(void) {
    reset();
    /* The specification puts it on a sixteen-byte boundary, and scanning every
       byte instead would take far longer for nothing. */
    place_rsdp(2048 + 8U, 0, other_storage, NULL);
    CHECK(acpi_find_rsdp(region, sizeof region) == NULL);
}

static void collects_the_tables_an_rsdt_lists(void) {
    reset();
    begin_table(madt_storage, ACPI_SIGNATURE_MADT, 64);
    finish_table(madt_storage);

    const void *pointers[] = {madt_storage, other_storage};
    build_root(rsdt_storage, "RSDT", false, pointers, 2);
    const uint8_t *rsdp = place_rsdp(64, 0, rsdt_storage, NULL);

    CHECK(acpi_collect(rsdp, map_table, NULL, &tables));
    CHECK(tables.count == 2);
    CHECK(tables.revision == 0);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_MADT) == madt_storage);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_FADT) == other_storage);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_MCFG) == NULL);
}

static void prefers_the_xsdt_when_there_is_one(void) {
    reset();
    begin_table(madt_storage, ACPI_SIGNATURE_MADT, 64);
    finish_table(madt_storage);

    /* The RSDT lists nothing; only the XSDT has the table. On a machine with
       tables above four gigabytes the RSDT cannot name them at all. */
    const void *none[] = {NULL};
    build_root(rsdt_storage, "RSDT", false, none, 0);
    const void *wide[] = {madt_storage};
    build_root(xsdt_storage, "XSDT", true, wide, 1);

    const uint8_t *rsdp = place_rsdp(64, 2, rsdt_storage, xsdt_storage);
    CHECK(acpi_collect(rsdp, map_table, NULL, &tables));
    CHECK(tables.count == 1);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_MADT) == madt_storage);
}

static void a_table_with_a_bad_checksum_is_dropped(void) {
    reset();
    begin_table(madt_storage, ACPI_SIGNATURE_MADT, 64);
    finish_table(madt_storage);
    madt_storage[20] ^= 0xFF;                 /* after the checksum was fixed */

    const void *pointers[] = {madt_storage, other_storage};
    build_root(rsdt_storage, "RSDT", false, pointers, 2);
    const uint8_t *rsdp = place_rsdp(64, 0, rsdt_storage, NULL);

    CHECK(acpi_collect(rsdp, map_table, NULL, &tables));
    /* The good one is kept; the bad one is not, and nothing walks into it. */
    CHECK(tables.count == 1);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_MADT) == NULL);
    CHECK(acpi_find_table(&tables, ACPI_SIGNATURE_FADT) == other_storage);
}

static void a_root_that_cannot_be_believed_stops_the_walk(void) {
    reset();
    const void *pointers[] = {other_storage};
    build_root(rsdt_storage, "RSDT", false, pointers, 1);
    rsdt_storage[10] ^= 0xFF;

    const uint8_t *rsdp = place_rsdp(64, 0, rsdt_storage, NULL);
    CHECK(!acpi_collect(rsdp, map_table, NULL, &tables));
    CHECK(tables.count == 0);

    /* And a length that could not be a table at all. */
    build_root(rsdt_storage, "RSDT", false, pointers, 1);
    put_u32(rsdt_storage + ACPI_SDT_LENGTH_OFFSET, 4);
    CHECK(!acpi_collect(place_rsdp(64, 0, rsdt_storage, NULL), map_table, NULL, &tables));
}

/* One MADT with two processors and an I/O APIC between them. */
static void build_madt(void) {
    uint32_t offset = ACPI_MADT_ENTRIES_OFFSET;
    begin_table(madt_storage, ACPI_SIGNATURE_MADT, 0);
    put_u32(madt_storage + ACPI_MADT_LOCAL_APIC_OFFSET, 0xFEE00000);

    uint8_t *at = madt_storage + offset;
    at[0] = ACPI_MADT_LOCAL_APIC;
    at[1] = 8;
    at[ACPI_MADT_LOCAL_APIC_ID_OFFSET] = 0;
    put_u32(at + ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET, ACPI_MADT_PROCESSOR_ENABLED);
    offset += 8;

    at = madt_storage + offset;
    at[0] = ACPI_MADT_IO_APIC;
    at[1] = 12;
    offset += 12;

    at = madt_storage + offset;
    at[0] = ACPI_MADT_LOCAL_APIC;
    at[1] = 8;
    at[ACPI_MADT_LOCAL_APIC_ID_OFFSET] = 1;
    put_u32(at + ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET, ACPI_MADT_PROCESSOR_ENABLED);
    offset += 8;

    /* A processor the firmware says not to start. */
    at = madt_storage + offset;
    at[0] = ACPI_MADT_LOCAL_APIC;
    at[1] = 8;
    at[ACPI_MADT_LOCAL_APIC_ID_OFFSET] = 2;
    put_u32(at + ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET, 0);
    offset += 8;

    put_u32(madt_storage + ACPI_SDT_LENGTH_OFFSET, offset);
    finish_table(madt_storage);

    const void *pointers[] = {madt_storage};
    build_root(rsdt_storage, "RSDT", false, pointers, 1);
}

static void lists_the_processors_the_madt_describes(void) {
    struct acpi_processors processors;
    reset();
    build_madt();

    CHECK(acpi_collect(place_rsdp(64, 0, rsdt_storage, NULL), map_table, NULL, &tables));
    CHECK(acpi_processors(&tables, &processors));

    /* Three processors, and the entry between them that is not one. */
    CHECK(processors.count == 3);
    CHECK(processors.local_apic_address == 0xFEE00000);
    CHECK(processors.entries[0].apic_id == 0 && processors.entries[0].enabled);
    CHECK(processors.entries[1].apic_id == 1 && processors.entries[1].enabled);
    CHECK(processors.entries[2].apic_id == 2 && !processors.entries[2].enabled);
}

static void an_entry_that_does_not_advance_ends_the_walk(void) {
    struct acpi_processors processors;
    reset();
    build_madt();
    /* A zero-length record is a list that never ends. */
    madt_storage[ACPI_MADT_ENTRIES_OFFSET + 1U] = 0;
    finish_table(madt_storage);

    CHECK(acpi_collect(place_rsdp(64, 0, rsdt_storage, NULL), map_table, NULL, &tables));
    CHECK(!acpi_processors(&tables, &processors));
    CHECK(processors.count == 0);
}

static void an_entry_reaching_past_the_table_is_not_read(void) {
    struct acpi_processors processors;
    reset();
    build_madt();
    madt_storage[ACPI_MADT_ENTRIES_OFFSET + 1U] = 200;
    finish_table(madt_storage);

    CHECK(acpi_collect(place_rsdp(64, 0, rsdt_storage, NULL), map_table, NULL, &tables));
    CHECK(!acpi_processors(&tables, &processors));
}

static void a_machine_with_no_madt_has_no_processor_list(void) {
    struct acpi_processors processors;
    reset();
    const void *pointers[] = {other_storage};
    build_root(rsdt_storage, "RSDT", false, pointers, 1);

    CHECK(acpi_collect(place_rsdp(64, 0, rsdt_storage, NULL), map_table, NULL, &tables));
    CHECK(!acpi_processors(&tables, &processors));
    CHECK(processors.count == 0);
}

TEST_MAIN(
    a_checksum_is_the_bytes_adding_to_zero();
    recognises_an_rsdp();
    a_bad_checksum_is_not_an_rsdp();
    a_revision_two_rsdp_needs_both_checksums();
    a_nonsense_length_is_refused();
    finds_an_rsdp_in_a_region();
    an_rsdp_off_the_alignment_is_not_found();
    collects_the_tables_an_rsdt_lists();
    prefers_the_xsdt_when_there_is_one();
    a_table_with_a_bad_checksum_is_dropped();
    a_root_that_cannot_be_believed_stops_the_walk();
    lists_the_processors_the_madt_describes();
    an_entry_that_does_not_advance_ends_the_walk();
    an_entry_reaching_past_the_table_is_not_read();
    a_machine_with_no_madt_has_no_processor_list();
)
