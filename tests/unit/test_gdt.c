#include "cpu/gdt.h"
#include "harness.h"
#include "util/mem.h"

#define ACCESS_BYTE 5
#define FLAGS_BYTE 6

static void encodes_a_descriptor_the_way_the_cpu_reads_it(void) {
    uint8_t entry[GDT_ENTRY_BYTES];
    CHECK(gdt_encode(entry, 0x12345678, 0xFEDCB, 0x9A, 0xA0));

    CHECK(entry[0] == 0xCB);
    CHECK(entry[1] == 0xED);
    CHECK(entry[2] == 0x78);
    CHECK(entry[3] == 0x56);
    CHECK(entry[4] == 0x34);
    CHECK(entry[ACCESS_BYTE] == 0x9A);
    /* Top nibble of the limit shares a byte with the flags. */
    CHECK(entry[FLAGS_BYTE] == 0xAF);
    CHECK(entry[7] == 0x12);
}

static void refuses_what_a_descriptor_cannot_hold(void) {
    uint8_t entry[GDT_ENTRY_BYTES];
    CHECK(!gdt_encode(entry, 0, 0x100000, 0x9A, 0xA0));
    /* The low nibble of the flags byte belongs to the limit. */
    CHECK(!gdt_encode(entry, 0, 0xFFFF, 0x9A, 0x0F));
}

static void the_table_holds_all_three_widths(void) {
    uint8_t table[GDT_ENTRY_COUNT * GDT_ENTRY_BYTES];
    memset(table, 0xAA, sizeof table);
    gdt_build(table, sizeof table);

    for (size_t index = 0; index < GDT_ENTRY_BYTES; index++)
        CHECK(table[GDT_SELECTOR_NULL + index] == 0);

    /* Long mode set, 32-bit size clear: setting both is illegal. */
    CHECK((table[GDT_SELECTOR_CODE64 + FLAGS_BYTE] & 0x20) != 0);
    CHECK((table[GDT_SELECTOR_CODE64 + FLAGS_BYTE] & 0x40) == 0);

    CHECK((table[GDT_SELECTOR_CODE32 + FLAGS_BYTE] & 0x40) != 0);
    CHECK((table[GDT_SELECTOR_CODE32 + FLAGS_BYTE] & 0x20) == 0);

    /* The 16-bit pair is byte granular so a thunk sees a 64 KiB segment. */
    CHECK((table[GDT_SELECTOR_CODE16 + FLAGS_BYTE] & 0x80) == 0);
    CHECK(table[GDT_SELECTOR_CODE16 + 0] == 0xFF);
    CHECK(table[GDT_SELECTOR_CODE16 + 1] == 0xFF);

    CHECK((table[GDT_SELECTOR_CODE64 + ACCESS_BYTE] & 0x08) != 0);
    CHECK((table[GDT_SELECTOR_DATA64 + ACCESS_BYTE] & 0x08) == 0);
}

static void a_short_buffer_is_left_alone(void) {
    uint8_t table[GDT_ENTRY_BYTES];
    memset(table, 0xAA, sizeof table);
    gdt_build(table, sizeof table);
    CHECK(table[0] == 0xAA);
}

TEST_MAIN(
    encodes_a_descriptor_the_way_the_cpu_reads_it();
    refuses_what_a_descriptor_cannot_hold();
    the_table_holds_all_three_widths();
    a_short_buffer_is_left_alone();
)
