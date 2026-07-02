#include "util/mem.h"

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t index = 0; index < count; index++) out[index] = in[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out == in || count == 0) return destination;
    if (out < in) {
        for (size_t index = 0; index < count; index++) out[index] = in[index];
    } else {
        for (size_t index = count; index > 0; index--) out[index - 1] = in[index - 1];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *out = (uint8_t *)destination;
    for (size_t index = 0; index < count; index++) out[index] = (uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t index = 0; index < count; index++) {
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    }
    return 0;
}
