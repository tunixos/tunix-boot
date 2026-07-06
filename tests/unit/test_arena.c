#include "harness.h"
#include "memory/arena.h"

#define BACKING_BYTES 1024U

static uint8_t backing[BACKING_BYTES] __attribute__((aligned(64)));

static struct arena fresh(void) {
    struct arena arena;
    arena_init(&arena, backing, sizeof backing);
    return arena;
}

static void hands_back_aligned_memory(void) {
    struct arena arena = fresh();
    /* One byte first, so the next allocation must be padded to align. */
    CHECK(arena_allocate(&arena, 1) != NULL);
    void *second = arena_allocate(&arena, 8);
    CHECK(second != NULL);
    CHECK(((uintptr_t)second % ARENA_DEFAULT_ALIGNMENT) == 0);

    void *wide = arena_allocate_aligned(&arena, 8, 64);
    CHECK(wide != NULL);
    CHECK(((uintptr_t)wide % 64) == 0);
}

static void refuses_what_does_not_fit(void) {
    struct arena arena = fresh();
    CHECK(arena_allocate(&arena, BACKING_BYTES + 1) == NULL);
    /* A refused allocation must not have consumed anything. */
    CHECK(arena_used(&arena) == 0);
    CHECK(arena_allocate(&arena, BACKING_BYTES) != NULL);
    CHECK(arena_allocate(&arena, 1) == NULL);
}

static void counts_the_padding_it_added(void) {
    struct arena arena = fresh();
    CHECK(arena_allocate(&arena, 1) != NULL);
    size_t after_first = arena_used(&arena);
    CHECK(arena_allocate_aligned(&arena, 1, 64) != NULL);
    /* The padding is charged to the arena, not silently borrowed. */
    CHECK(arena_used(&arena) > after_first + 1);
    CHECK(arena_used(&arena) + arena_available(&arena) == BACKING_BYTES);
}

static void rejects_an_alignment_it_cannot_honour(void) {
    struct arena arena = fresh();
    CHECK(arena_allocate_aligned(&arena, 8, 0) == NULL);
    CHECK(arena_allocate_aligned(&arena, 8, 3) == NULL);
    CHECK(arena_used(&arena) == 0);
}

static void an_array_that_would_wrap_is_refused(void) {
    struct arena arena = fresh();
    CHECK(arena_allocate_array(&arena, 4, 8) != NULL);
    CHECK(arena_allocate_array(&arena, (size_t)-1, 2) == NULL);
    CHECK(arena_allocate_array(&arena, 2, (size_t)-1) == NULL);
}

static void zeroed_memory_is_actually_zero(void) {
    struct arena arena = fresh();
    for (size_t index = 0; index < BACKING_BYTES; index++) backing[index] = 0xAA;
    arena_reset(&arena);

    uint8_t *memory = (uint8_t *)arena_allocate_zeroed(&arena, 32);
    CHECK(memory != NULL);
    for (size_t index = 0; index < 32; index++) CHECK(memory[index] == 0);
}

static void a_mark_winds_back_only_what_came_after_it(void) {
    struct arena arena = fresh();
    CHECK(arena_allocate(&arena, 64) != NULL);
    struct arena_mark mark = arena_mark(&arena);
    size_t before = arena_used(&arena);

    CHECK(arena_allocate(&arena, 128) != NULL);
    CHECK(arena_used(&arena) > before);

    arena_release(&arena, mark);
    CHECK(arena_used(&arena) == before);

    /* Releasing to a mark ahead of the cursor must not push it forward. */
    struct arena_mark stale = {BACKING_BYTES};
    arena_release(&arena, stale);
    CHECK(arena_used(&arena) == before);
}

static void an_empty_arena_allocates_nothing(void) {
    struct arena arena;
    arena_init(&arena, NULL, BACKING_BYTES);
    CHECK(arena_available(&arena) == 0);
    CHECK(arena_allocate(&arena, 1) == NULL);
}

TEST_MAIN(
    hands_back_aligned_memory();
    refuses_what_does_not_fit();
    counts_the_padding_it_added();
    rejects_an_alignment_it_cannot_honour();
    an_array_that_would_wrap_is_refused();
    zeroed_memory_is_actually_zero();
    a_mark_winds_back_only_what_came_after_it();
    an_empty_arena_allocates_nothing();
)
