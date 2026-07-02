#ifndef TUNIX_BOOT_UTIL_READER_H
#define TUNIX_BOOT_UTIL_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A cursor over a buffer whose contents are not trusted.
 *
 * Every field a partition table, filesystem, ELF header, ACPI table or
 * configuration file offers is read through one of these. A read that would
 * leave the buffer returns false and moves nothing, so a caller that checks
 * its return value cannot be walked off the end by crafted input.
 */
struct reader {
    const uint8_t *base;
    size_t length;
    size_t position;
};

void reader_init(struct reader *reader, const void *base, size_t length);

/* Bytes still ahead of the cursor. */
size_t reader_remaining(const struct reader *reader);

/* Move the cursor to an absolute offset. False if it is out of range. */
bool reader_seek(struct reader *reader, size_t offset);

/* Move the cursor forward. False on overflow or past the end. */
bool reader_skip(struct reader *reader, size_t count);

bool reader_u8(struct reader *reader, uint8_t *out);
bool reader_u16(struct reader *reader, uint16_t *out);
bool reader_u32(struct reader *reader, uint32_t *out);
bool reader_u64(struct reader *reader, uint64_t *out);

/* Copy `count` bytes out. Nothing is copied unless all of it is available. */
bool reader_bytes(struct reader *reader, void *out, size_t count);

/*
 * A pointer to `count` bytes at the cursor, without copying, advancing past
 * them. NULL when they are not all present. The pointer borrows the reader's
 * buffer and lives exactly as long as it does.
 */
const void *reader_take(struct reader *reader, size_t count);

/* Read without moving the cursor. */
bool reader_peek_u8(const struct reader *reader, size_t offset, uint8_t *out);

#endif
