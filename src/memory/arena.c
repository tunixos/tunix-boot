#include "memory/arena.h"
#include "util/checked.h"
#include "util/mem.h"

static bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void arena_init(struct arena *arena, void *base, size_t capacity) {
    arena->base = (uint8_t *)base;
    arena->capacity = base ? capacity : 0;
    arena->used = 0;
}

void *arena_allocate_aligned(struct arena *arena, size_t size, size_t alignment) {
    if (!is_power_of_two(alignment)) return NULL;
    if (size == 0) return NULL;

    /* The alignment is applied to the address, not the offset, so an arena whose
       base is itself misaligned still hands back aligned memory. */
    uintptr_t current = (uintptr_t)arena->base + arena->used;
    uintptr_t aligned = (current + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    size_t padding = (size_t)(aligned - current);

    size_t needed;
    if (!checked_add_size(padding, size, &needed)) return NULL;
    if (needed > arena->capacity - arena->used) return NULL;

    arena->used += needed;
    return (void *)aligned;
}

void *arena_allocate(struct arena *arena, size_t size) {
    return arena_allocate_aligned(arena, size, ARENA_DEFAULT_ALIGNMENT);
}

void *arena_allocate_zeroed(struct arena *arena, size_t size) {
    void *memory = arena_allocate(arena, size);
    if (memory) memset(memory, 0, size);
    return memory;
}

void *arena_allocate_array(struct arena *arena, size_t count, size_t size) {
    size_t total;
    if (!checked_mul_size(count, size, &total)) return NULL;
    return arena_allocate(arena, total);
}

size_t arena_used(const struct arena *arena) {
    return arena->used;
}

size_t arena_available(const struct arena *arena) {
    return arena->capacity - arena->used;
}

void arena_reset(struct arena *arena) {
    arena->used = 0;
}

struct arena_mark arena_mark(const struct arena *arena) {
    struct arena_mark mark = {arena->used};
    return mark;
}

void arena_release(struct arena *arena, struct arena_mark mark) {
    if (mark.used <= arena->used) arena->used = mark.used;
}
