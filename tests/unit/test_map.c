#include "harness.h"
#include "memory/map.h"

#define KIB 1024ULL
#define MIB (1024ULL * KIB)
#define GIB (1024ULL * MIB)

static struct memory_map map;

static void refuses_what_cannot_be_a_region(void) {
    memory_map_clear(&map);
    CHECK(!memory_map_add(&map, 0, 0, MEMORY_KIND_USABLE));
    /* An end that wraps would compare below its own base everywhere after. */
    CHECK(!memory_map_add(&map, UINT64_MAX - 4, 16, MEMORY_KIND_USABLE));
    CHECK(map.count == 0);
}

static void sorts_what_arrived_out_of_order(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 4 * MIB, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 0, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 2 * MIB, MIB, MEMORY_KIND_RESERVED));
    memory_map_finalize(&map);

    CHECK(map.count == 3);
    CHECK(map.regions[0].base == 0);
    CHECK(map.regions[1].base == 2 * MIB);
    CHECK(map.regions[2].base == 4 * MIB);
}

static void merges_what_touches(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, MIB, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 2 * MIB, MIB, MEMORY_KIND_USABLE));
    memory_map_finalize(&map);

    CHECK(map.count == 1);
    CHECK(map.regions[0].base == 0);
    CHECK(map.regions[0].length == 3 * MIB);
}

static void leaves_a_gap_alone(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 2 * MIB, MIB, MEMORY_KIND_USABLE));
    memory_map_finalize(&map);

    /* Memory nobody described is not memory we may use. */
    CHECK(map.count == 2);
    CHECK(memory_map_find(&map, MIB + KIB) == NULL);
}

static void reserved_wins_the_bytes_it_overlaps(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 8 * MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 2 * MIB, 2 * MIB, MEMORY_KIND_RESERVED));
    memory_map_finalize(&map);

    CHECK(map.count == 3);
    CHECK(map.regions[0].kind == MEMORY_KIND_USABLE);
    CHECK(map.regions[0].base == 0 && map.regions[0].length == 2 * MIB);
    CHECK(map.regions[1].kind == MEMORY_KIND_RESERVED);
    CHECK(map.regions[1].base == 2 * MIB && map.regions[1].length == 2 * MIB);
    CHECK(map.regions[2].kind == MEMORY_KIND_USABLE);
    CHECK(map.regions[2].base == 4 * MIB && map.regions[2].length == 4 * MIB);

    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 6 * MIB);
    CHECK(memory_map_total(&map, MEMORY_KIND_RESERVED) == 2 * MIB);
}

static void the_more_restrictive_kind_wins_either_order(void) {
    for (int reversed = 0; reversed < 2; reversed++) {
        memory_map_clear(&map);
        if (reversed) {
            CHECK(memory_map_add(&map, 0, 2 * MIB, MEMORY_KIND_ACPI_NVS));
            CHECK(memory_map_add(&map, 0, 2 * MIB, MEMORY_KIND_USABLE));
        } else {
            CHECK(memory_map_add(&map, 0, 2 * MIB, MEMORY_KIND_USABLE));
            CHECK(memory_map_add(&map, 0, 2 * MIB, MEMORY_KIND_ACPI_NVS));
        }
        memory_map_finalize(&map);

        CHECK(map.count == 1);
        CHECK(map.regions[0].kind == MEMORY_KIND_ACPI_NVS);
        CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 0);
    }
}

static void three_deep_overlap_resolves(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 6 * MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, MIB, 4 * MIB, MEMORY_KIND_ACPI_RECLAIMABLE));
    CHECK(memory_map_add(&map, 2 * MIB, 2 * MIB, MEMORY_KIND_BAD));
    memory_map_finalize(&map);

    /* usable | acpi | bad | acpi | usable, and nothing lost in between. */
    CHECK(map.count == 5);
    CHECK(map.regions[2].kind == MEMORY_KIND_BAD);
    CHECK(map.regions[2].base == 2 * MIB && map.regions[2].length == 2 * MIB);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 2 * MIB);
    CHECK(memory_map_total(&map, MEMORY_KIND_ACPI_RECLAIMABLE) == 2 * MIB);

    uint64_t covered = 0;
    for (size_t index = 0; index < map.count; index++)
        covered += map.regions[index].length;
    CHECK(covered == 6 * MIB);
}

static void a_finalized_map_is_disjoint_and_ascending(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 5 * MIB, 3 * MIB, MEMORY_KIND_RESERVED));
    CHECK(memory_map_add(&map, 0, 7 * MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 6 * MIB, 4 * MIB, MEMORY_KIND_ACPI_NVS));
    CHECK(memory_map_add(&map, MIB, MIB, MEMORY_KIND_BAD));
    memory_map_finalize(&map);

    for (size_t index = 1; index < map.count; index++) {
        struct memory_region previous = map.regions[index - 1];
        CHECK(map.regions[index].base >= previous.base + previous.length);
        CHECK(map.regions[index].length > 0);
    }
}

/* An E820 map shaped like the ones QEMU and real firmware hand over: the low
   megabyte carved up, a hole at the BIOS area, and a second run above 4 GiB. */
static void a_realistic_firmware_map(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 0x9FC00, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 0x9FC00, 0x400, MEMORY_KIND_RESERVED));
    CHECK(memory_map_add(&map, 0xF0000, 0x10000, MEMORY_KIND_RESERVED));
    CHECK(memory_map_add(&map, MIB, 2047 * MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 0x7FFF0000, 0x10000, MEMORY_KIND_ACPI_RECLAIMABLE));
    CHECK(memory_map_add(&map, 0xFFFC0000, 0x40000, MEMORY_KIND_RESERVED));
    CHECK(memory_map_add(&map, 4 * GIB, 2 * GIB, MEMORY_KIND_USABLE));
    memory_map_finalize(&map);

    CHECK(!map.truncated);
    CHECK(memory_map_find(&map, 0x9FC00)->kind == MEMORY_KIND_RESERVED);
    CHECK(memory_map_find(&map, 0xA0000) == NULL);
    CHECK(memory_map_find(&map, 5 * GIB)->kind == MEMORY_KIND_USABLE);

    /* The ACPI region overlaps the top of the big usable run and must take it. */
    CHECK(memory_map_find(&map, 0x7FFF8000)->kind == MEMORY_KIND_ACPI_RECLAIMABLE);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) ==
          0x9FC00 + (2047 * MIB - 0x10000) + 2 * GIB);
}

static void finds_aligned_room_above_a_floor(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 16 * MIB, 16 * MIB, MEMORY_KIND_USABLE));
    memory_map_finalize(&map);

    uint64_t found = 0;
    CHECK(memory_map_find_free(&map, 4 * KIB, 4 * KIB, 0, &found));
    CHECK(found == 0);

    /* The floor pushes it past the first region, which cannot hold it anyway. */
    CHECK(memory_map_find_free(&map, 8 * MIB, 2 * MIB, MIB, &found));
    CHECK(found == 16 * MIB);

    CHECK(!memory_map_find_free(&map, 32 * MIB, 4 * KIB, 0, &found));
    CHECK(!memory_map_find_free(&map, 4 * KIB, 3, 0, &found));
    CHECK(!memory_map_find_free(&map, 0, 4 * KIB, 0, &found));
}

static void never_hands_out_memory_it_did_not_own(void) {
    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 16 * MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&map, 4 * MIB, 4 * MIB, MEMORY_KIND_RESERVED));
    memory_map_finalize(&map);

    uint64_t found = 0;
    /* 8 MiB aligned to 8 MiB would land at 0 and run straight through the
       reserved hole; the only honest answer is the region above it. */
    CHECK(memory_map_find_free(&map, 8 * MIB, 8 * MIB, 0, &found));
    CHECK(found == 8 * MIB);
    CHECK(memory_map_find(&map, found)->kind == MEMORY_KIND_USABLE);
    CHECK(memory_map_find(&map, found + 8 * MIB - 1)->kind == MEMORY_KIND_USABLE);
}

static void a_full_map_says_so(void) {
    memory_map_clear(&map);
    for (unsigned index = 0; index < MEMORY_MAP_MAX_REGIONS; index++) {
        CHECK(memory_map_add(&map, index * 2 * MIB, MIB, MEMORY_KIND_USABLE));
    }
    CHECK(!map.truncated);
    CHECK(!memory_map_add(&map, 64 * GIB, MIB, MEMORY_KIND_USABLE));
    CHECK(map.truncated);
    CHECK(map.count == MEMORY_MAP_MAX_REGIONS);
}

static void names_every_kind(void) {
    /* A kind added without a name would print as the fallback in the one log
       line anyone reads when a machine will not boot. */
    const char *fallback = memory_kind_name((enum memory_kind)(MEMORY_KIND_BAD + 1));
    for (int kind = MEMORY_KIND_USABLE; kind <= MEMORY_KIND_BAD; kind++) {
        CHECK(memory_kind_name((enum memory_kind)kind) != fallback);
    }
}

TEST_MAIN(
    refuses_what_cannot_be_a_region();
    sorts_what_arrived_out_of_order();
    merges_what_touches();
    leaves_a_gap_alone();
    reserved_wins_the_bytes_it_overlaps();
    the_more_restrictive_kind_wins_either_order();
    three_deep_overlap_resolves();
    a_finalized_map_is_disjoint_and_ascending();
    a_realistic_firmware_map();
    finds_aligned_room_above_a_floor();
    never_hands_out_memory_it_did_not_own();
    a_full_map_says_so();
    names_every_kind();
)
