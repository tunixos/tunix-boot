#include "harness.h"
#include "memory/paging.h"

#define ARENA_BYTES (2U * 1024U * 1024U)
#define HIGHER_HALF 0xFFFFFFFF80000000ULL
#define FAKE_PHYSICAL 0x200000ULL

/*
 * The tables are built in host memory and the addresses written into the
 * entries are host pointers, which is exactly what happens on the target: the
 * loader runs identity mapped, so a physical address is a usable pointer. That
 * makes paging_translate a real walk of real tables rather than a model of one.
 */
static uint8_t backing[ARENA_BYTES] __attribute__((aligned(4096)));
static struct arena arena;
static struct page_tables tables;

static void setup(bool huge_pages) {
    arena_init(&arena, backing, sizeof backing);
    CHECK(paging_create(&tables, &arena, huge_pages));
}

static bool maps_to(uint64_t virtual, uint64_t physical) {
    uint64_t got;
    if (!paging_translate(&tables, virtual, &got, NULL)) return false;
    return got == physical;
}

static void a_new_table_maps_nothing(void) {
    setup(false);
    CHECK(tables.root != 0);
    CHECK(!paging_translate(&tables, 0, NULL, NULL));
    CHECK(!paging_translate(&tables, HIGHER_HALF, NULL, NULL));
}

static void maps_and_translates_a_page(void) {
    setup(false);
    CHECK(paging_map(&tables, 0x1000, 0x5000, PAGE_BYTES, PAGE_WRITABLE));

    CHECK(maps_to(0x1000, 0x5000));
    /* An offset within the page comes back with the offset kept. */
    CHECK(maps_to(0x1000 + 0x123, 0x5000 + 0x123));
    /* The pages either side belong to nobody. */
    CHECK(!paging_translate(&tables, 0, NULL, NULL));
    CHECK(!paging_translate(&tables, 0x2000, NULL, NULL));
}

static void carries_the_flags_it_was_given(void) {
    uint64_t flags;
    setup(false);
    CHECK(paging_map(&tables, 0x1000, 0x5000, PAGE_BYTES,
                     PAGE_WRITABLE | PAGE_NO_EXECUTE));
    CHECK(paging_map(&tables, 0x2000, 0x6000, PAGE_BYTES, 0));

    CHECK(paging_translate(&tables, 0x1000, NULL, &flags));
    CHECK(flags & PAGE_PRESENT);
    CHECK(flags & PAGE_WRITABLE);
    CHECK(flags & PAGE_NO_EXECUTE);

    /* A read-only, executable page must not come back writable because the
       table above it had to be. */
    CHECK(paging_translate(&tables, 0x2000, NULL, &flags));
    CHECK(flags & PAGE_PRESENT);
    CHECK(!(flags & PAGE_WRITABLE));
    CHECK(!(flags & PAGE_NO_EXECUTE));
}

static void maps_a_range_across_tables(void) {
    setup(false);
    /* Straddles a page-table boundary, where the walk has to build a second
       leaf table rather than run off the end of the first. */
    const uint64_t base = PAGE_LARGE_BYTES - 8U * PAGE_BYTES;
    CHECK(paging_map(&tables, base, FAKE_PHYSICAL, 16U * PAGE_BYTES, 0));

    for (unsigned index = 0; index < 16; index++) {
        CHECK(maps_to(base + index * PAGE_BYTES,
                      FAKE_PHYSICAL + index * PAGE_BYTES));
    }
    CHECK(!paging_translate(&tables, base + 16U * PAGE_BYTES, NULL, NULL));
}

static void uses_a_large_page_when_everything_lines_up(void) {
    uint64_t flags;
    setup(false);
    CHECK(paging_map(&tables, PAGE_LARGE_BYTES, PAGE_LARGE_BYTES,
                     PAGE_LARGE_BYTES, PAGE_WRITABLE));

    CHECK(paging_translate(&tables, PAGE_LARGE_BYTES, NULL, &flags));
    CHECK(flags & PAGE_SIZE_LARGE);
    CHECK(maps_to(PAGE_LARGE_BYTES + 0x54321, PAGE_LARGE_BYTES + 0x54321));

    /* One 2 MiB entry rather than 512 of 4 KiB, so the tables cost a level
       less; the whole point of using it. */
    CHECK(arena_used(&arena) <= 4U * PAGE_TABLE_BYTES);
}

static void falls_back_to_small_pages_when_alignment_forbids(void) {
    uint64_t flags;
    setup(false);
    /* Aligned virtually, not physically: a 2 MiB entry cannot express this. */
    CHECK(paging_map(&tables, PAGE_LARGE_BYTES, PAGE_BYTES, PAGE_LARGE_BYTES, 0));

    CHECK(paging_translate(&tables, PAGE_LARGE_BYTES, NULL, &flags));
    CHECK(!(flags & PAGE_SIZE_LARGE));
    CHECK(maps_to(PAGE_LARGE_BYTES, PAGE_BYTES));
    CHECK(maps_to(PAGE_LARGE_BYTES + PAGE_BYTES, 2U * PAGE_BYTES));
}

static void a_partial_range_ends_in_small_pages(void) {
    setup(false);
    /* Two megabytes and one page: the tail cannot be a large page. */
    const uint64_t bytes = PAGE_LARGE_BYTES + PAGE_BYTES;
    CHECK(paging_map(&tables, PAGE_LARGE_BYTES, PAGE_LARGE_BYTES, bytes, 0));

    uint64_t flags;
    CHECK(paging_translate(&tables, PAGE_LARGE_BYTES, NULL, &flags));
    CHECK(flags & PAGE_SIZE_LARGE);

    const uint64_t tail = PAGE_LARGE_BYTES + PAGE_LARGE_BYTES;
    CHECK(paging_translate(&tables, tail, NULL, &flags));
    CHECK(!(flags & PAGE_SIZE_LARGE));
    CHECK(maps_to(tail, tail));
    CHECK(!paging_translate(&tables, tail + PAGE_BYTES, NULL, NULL));
}

static void uses_a_gigabyte_page_only_when_the_processor_has_them(void) {
    uint64_t flags;
    setup(true);
    CHECK(paging_map(&tables, PAGE_HUGE_BYTES, PAGE_HUGE_BYTES, PAGE_HUGE_BYTES,
                     PAGE_WRITABLE));
    CHECK(paging_translate(&tables, PAGE_HUGE_BYTES, NULL, &flags));
    CHECK(flags & PAGE_SIZE_LARGE);
    CHECK(maps_to(PAGE_HUGE_BYTES + 0x1234567, PAGE_HUGE_BYTES + 0x1234567));

    /* A gigabyte page is a leaf one level up, so the page directory below it is
       never made: root and pointer table, and nothing else. */
    CHECK(arena_used(&arena) == 2U * PAGE_TABLE_BYTES);

    /* Without them the same gigabyte is 512 large pages, which all fit in one
       directory — so the cost is exactly one more table, not five hundred. */
    setup(false);
    CHECK(paging_map(&tables, PAGE_HUGE_BYTES, PAGE_HUGE_BYTES, PAGE_HUGE_BYTES,
                     PAGE_WRITABLE));
    CHECK(maps_to(PAGE_HUGE_BYTES + 0x1234567, PAGE_HUGE_BYTES + 0x1234567));
    CHECK(arena_used(&arena) == 3U * PAGE_TABLE_BYTES);

    CHECK(paging_translate(&tables, PAGE_HUGE_BYTES, NULL, &flags));
    CHECK(flags & PAGE_SIZE_LARGE);
}

static void maps_a_kernel_into_the_higher_half(void) {
    setup(false);
    /* What a kernel linked at -2 GiB actually asks for. */
    CHECK(paging_map(&tables, HIGHER_HALF, FAKE_PHYSICAL, 4U * PAGE_LARGE_BYTES,
                     PAGE_WRITABLE));

    CHECK(maps_to(HIGHER_HALF, FAKE_PHYSICAL));
    CHECK(maps_to(HIGHER_HALF + 0x100000, FAKE_PHYSICAL + 0x100000));
    /* The same physical memory can also be reachable at its own address. */
    CHECK(paging_map(&tables, FAKE_PHYSICAL, FAKE_PHYSICAL,
                     4U * PAGE_LARGE_BYTES, PAGE_WRITABLE));
    CHECK(maps_to(FAKE_PHYSICAL, FAKE_PHYSICAL));
}

static void refuses_a_non_canonical_address(void) {
    setup(false);
    CHECK(paging_address_is_canonical(0));
    CHECK(paging_address_is_canonical(0x00007FFFFFFFF000ULL));
    CHECK(paging_address_is_canonical(HIGHER_HALF));
    CHECK(!paging_address_is_canonical(0x0000800000000000ULL));
    CHECK(!paging_address_is_canonical(0x1234567800000000ULL));

    CHECK(!paging_map(&tables, 0x0000800000000000ULL, 0, PAGE_BYTES, 0));
    /* Starts canonical and runs into the hole, which is worse than starting in
       it because the first pages would map. */
    CHECK(!paging_map(&tables, 0x00007FFFFFFFF000ULL, 0, 2U * PAGE_BYTES, 0));
    CHECK(!paging_translate(&tables, 0x0000800000000000ULL, NULL, NULL));
}

static void refuses_unaligned_or_empty_requests(void) {
    setup(false);
    CHECK(!paging_map(&tables, 0x1001, 0x5000, PAGE_BYTES, 0));
    CHECK(!paging_map(&tables, 0x1000, 0x5001, PAGE_BYTES, 0));
    CHECK(!paging_map(&tables, 0x1000, 0x5000, 0x800, 0));
    CHECK(!paging_map(&tables, 0x1000, 0x5000, 0, 0));
    CHECK(arena_used(&arena) == PAGE_TABLE_BYTES);
}

static void refuses_flags_that_are_not_the_callers_to_set(void) {
    setup(false);
    /* An address smuggled in as flags would be OR-ed over the real one. */
    CHECK(!paging_map(&tables, 0x1000, 0x5000, PAGE_BYTES, 0x200000ULL));
    /* And this would make a 4 KiB entry claim to be a 2 MiB page. */
    CHECK(!paging_map(&tables, 0x1000, 0x5000, PAGE_BYTES, PAGE_SIZE_LARGE));
    CHECK(!paging_translate(&tables, 0x1000, NULL, NULL));
}

static void refuses_to_remap_what_is_already_mapped(void) {
    setup(false);
    CHECK(paging_map(&tables, 0x1000, 0x5000, PAGE_BYTES, 0));
    /* Silently changing a live translation is worse than refusing. */
    CHECK(!paging_map(&tables, 0x1000, 0x9000, PAGE_BYTES, 0));
    CHECK(maps_to(0x1000, 0x5000));

    /* And a small page may not be carved out of a large one already there. */
    CHECK(paging_map(&tables, PAGE_LARGE_BYTES, PAGE_LARGE_BYTES,
                     PAGE_LARGE_BYTES, 0));
    CHECK(!paging_map(&tables, PAGE_LARGE_BYTES + PAGE_BYTES, 0x9000,
                      PAGE_BYTES, 0));
}

static void running_out_of_tables_fails_rather_than_writes_anywhere(void) {
    static uint8_t small[3U * PAGE_TABLE_BYTES] __attribute__((aligned(4096)));
    struct arena tiny;
    arena_init(&tiny, small, sizeof small);
    CHECK(paging_create(&tables, &tiny, false));

    /* Room for the root and two levels below it, not the fourth. */
    CHECK(!paging_map(&tables, HIGHER_HALF, FAKE_PHYSICAL, PAGE_BYTES, 0));
    CHECK(!paging_translate(&tables, HIGHER_HALF, NULL, NULL));
}

TEST_MAIN(
    a_new_table_maps_nothing();
    maps_and_translates_a_page();
    carries_the_flags_it_was_given();
    maps_a_range_across_tables();
    uses_a_large_page_when_everything_lines_up();
    falls_back_to_small_pages_when_alignment_forbids();
    a_partial_range_ends_in_small_pages();
    uses_a_gigabyte_page_only_when_the_processor_has_them();
    maps_a_kernel_into_the_higher_half();
    refuses_a_non_canonical_address();
    refuses_unaligned_or_empty_requests();
    refuses_flags_that_are_not_the_callers_to_set();
    refuses_to_remap_what_is_already_mapped();
    running_out_of_tables_fails_rather_than_writes_anywhere();
)
