#include "util/reader.h"

#define BITS_PER_BYTE 8U

static bool reader_has(const struct reader *reader, size_t count) {
    return count <= reader->length - reader->position;
}

/* Little endian, because every field this reads is defined that way on x86. */
static uint64_t little_endian(const uint8_t *bytes, size_t width) {
    uint64_t value = 0;
    for (size_t index = 0; index < width; index++)
        value |= (uint64_t)bytes[index] << (index * BITS_PER_BYTE);
    return value;
}

static bool reader_scalar(struct reader *reader, size_t width, uint64_t *out) {
    if (!reader_has(reader, width)) return false;
    *out = little_endian(reader->base + reader->position, width);
    reader->position += width;
    return true;
}

void reader_init(struct reader *reader, const void *base, size_t length) {
    reader->base = (const uint8_t *)base;
    reader->length = base ? length : 0;
    reader->position = 0;
}

size_t reader_remaining(const struct reader *reader) {
    return reader->length - reader->position;
}

bool reader_seek(struct reader *reader, size_t offset) {
    if (offset > reader->length) return false;
    reader->position = offset;
    return true;
}

bool reader_skip(struct reader *reader, size_t count) {
    if (!reader_has(reader, count)) return false;
    reader->position += count;
    return true;
}

bool reader_u8(struct reader *reader, uint8_t *out) {
    uint64_t value;
    if (!reader_scalar(reader, sizeof(uint8_t), &value)) return false;
    *out = (uint8_t)value;
    return true;
}

bool reader_u16(struct reader *reader, uint16_t *out) {
    uint64_t value;
    if (!reader_scalar(reader, sizeof(uint16_t), &value)) return false;
    *out = (uint16_t)value;
    return true;
}

bool reader_u32(struct reader *reader, uint32_t *out) {
    uint64_t value;
    if (!reader_scalar(reader, sizeof(uint32_t), &value)) return false;
    *out = (uint32_t)value;
    return true;
}

bool reader_u64(struct reader *reader, uint64_t *out) {
    return reader_scalar(reader, sizeof(uint64_t), out);
}

bool reader_bytes(struct reader *reader, void *out, size_t count) {
    const void *source = reader_take(reader, count);
    if (!source) return false;
    uint8_t *destination = (uint8_t *)out;
    const uint8_t *bytes = (const uint8_t *)source;
    for (size_t index = 0; index < count; index++)
        destination[index] = bytes[index];
    return true;
}

const void *reader_take(struct reader *reader, size_t count) {
    if (!reader_has(reader, count)) return NULL;
    const void *at = reader->base + reader->position;
    reader->position += count;
    return at;
}

bool reader_peek_u8(const struct reader *reader, size_t offset, uint8_t *out) {
    if (offset >= reader_remaining(reader)) return false;
    *out = reader->base[reader->position + offset];
    return true;
}
