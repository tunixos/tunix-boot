#ifndef TUNIX_BOOT_UTIL_STR_H
#define TUNIX_BOOT_UTIL_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A string that knows its own length, so nothing here depends on a terminator
   that untrusted input may have omitted. */
struct str {
    const char *data;
    size_t length;
};

struct str str_from_cstr(const char *text);
struct str str_slice(struct str text, size_t offset, size_t length);
bool str_equal(struct str left, struct str right);
bool str_equal_cstr(struct str left, const char *right);
bool str_equal_ignore_case(struct str left, struct str right);
bool str_starts_with(struct str text, struct str prefix);
struct str str_trim(struct str text);

/* Parse a decimal or 0x-prefixed value. False on overflow or a bad digit. */
bool str_to_u64(struct str text, uint64_t *out);

/* Copy into a fixed buffer and terminate. False if it does not fit. */
bool str_copy_cstr(struct str text, char *out, size_t capacity);

size_t cstr_length(const char *text);

#endif
