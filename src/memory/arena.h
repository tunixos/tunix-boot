#ifndef TUNIX_BOOT_MEMORY_ARENA_H
#define TUNIX_BOOT_MEMORY_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A bump allocator, and deliberately nothing more.
 *
 * Everything a bootloader allocates lives until the kernel is entered, so the
 * generality of a heap buys nothing and costs the two failure modes a machine
 * with no debugger can least afford: fragmentation and leaks. Allocation here is
 * a bounds check and a pointer increment, and the only way to free is to throw
 * the whole arena away.
 *
 * A scratch arena serves parsing: reset it when the parse is done and every
 * temporary it made is gone at once.
 */
struct arena {
    uint8_t *base;
    size_t capacity;
    size_t used;
};

#define ARENA_DEFAULT_ALIGNMENT 16U

void arena_init(struct arena *arena, void *base, size_t capacity);

/* Aligned to `alignment`, which must be a power of two. NULL if it does not
   fit, or if the alignment is not one the allocator can honour. */
void *arena_allocate_aligned(struct arena *arena, size_t size, size_t alignment);

/* Aligned to ARENA_DEFAULT_ALIGNMENT, which suits anything the loader stores. */
void *arena_allocate(struct arena *arena, size_t size);

/* Zeroed. Separate from arena_allocate() so the cost is visible at the call. */
void *arena_allocate_zeroed(struct arena *arena, size_t size);

/* An array, refusing rather than wrapping when count * size overflows. */
void *arena_allocate_array(struct arena *arena, size_t count, size_t size);

size_t arena_used(const struct arena *arena);
size_t arena_available(const struct arena *arena);

/* Forget everything. The memory is not touched, so anything still pointing into
   the arena is now pointing at what the next allocation will overwrite. */
void arena_reset(struct arena *arena);

/*
 * A saved position an arena can be wound back to, for a scratch allocation that
 * has to be undone without discarding what came before it.
 */
struct arena_mark {
    size_t used;
};

struct arena_mark arena_mark(const struct arena *arena);
void arena_release(struct arena *arena, struct arena_mark mark);

#endif
