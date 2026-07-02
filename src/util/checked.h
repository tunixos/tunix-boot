#ifndef TUNIX_BOOT_UTIL_CHECKED_H
#define TUNIX_BOOT_UTIL_CHECKED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Arithmetic on sizes that came off a disk.
 *
 * A cluster count multiplied by a cluster size, an offset plus a length, an
 * entry count times an entry size: every one of these is an attacker-controlled
 * product, and every one of them wraps silently in C. These return false on
 * overflow instead, so a bad image becomes a rejected image rather than a short
 * allocation followed by a long copy.
 */

static inline bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    return !__builtin_add_overflow(a, b, out);
}

static inline bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    return !__builtin_mul_overflow(a, b, out);
}

static inline bool checked_add_size(size_t a, size_t b, size_t *out) {
    return !__builtin_add_overflow(a, b, out);
}

static inline bool checked_mul_size(size_t a, size_t b, size_t *out) {
    return !__builtin_mul_overflow(a, b, out);
}

/* Narrow a 64-bit value from an image to size_t, refusing if it does not fit. */
static inline bool checked_narrow_size(uint64_t value, size_t *out) {
    if (value > (uint64_t)(size_t)-1) return false;
    *out = (size_t)value;
    return true;
}

/* Round up to a power-of-two alignment, refusing on overflow. */
static inline bool checked_align_up_u64(uint64_t value, uint64_t alignment,
                                        uint64_t *out) {
    uint64_t sum;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return false;
    if (!checked_add_u64(value, alignment - 1, &sum)) return false;
    *out = sum & ~(alignment - 1);
    return true;
}

#endif
