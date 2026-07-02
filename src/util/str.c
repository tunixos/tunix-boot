#include "util/checked.h"
#include "util/str.h"

#define DECIMAL_BASE 10U
#define HEX_BASE 16U
#define CASE_BIT 0x20U

static bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static char lower(char value) {
    return (value >= 'A' && value <= 'Z') ? (char)(value | CASE_BIT) : value;
}

static bool digit_value(char value, uint64_t base, uint64_t *out) {
    uint64_t digit;
    if (value >= '0' && value <= '9') digit = (uint64_t)(value - '0');
    else if (value >= 'a' && value <= 'f') digit = (uint64_t)(value - 'a') + DECIMAL_BASE;
    else if (value >= 'A' && value <= 'F') digit = (uint64_t)(value - 'A') + DECIMAL_BASE;
    else return false;
    if (digit >= base) return false;
    *out = digit;
    return true;
}

size_t cstr_length(const char *text) {
    size_t length = 0;
    while (text && text[length] != '\0') length++;
    return length;
}

struct str str_from_cstr(const char *text) {
    struct str result = {text, cstr_length(text)};
    return result;
}

struct str str_slice(struct str text, size_t offset, size_t length) {
    struct str empty = {NULL, 0};
    if (offset > text.length) return empty;
    size_t available = text.length - offset;
    struct str result = {text.data + offset, length < available ? length : available};
    return result;
}

bool str_equal(struct str left, struct str right) {
    if (left.length != right.length) return false;
    for (size_t index = 0; index < left.length; index++) {
        if (left.data[index] != right.data[index]) return false;
    }
    return true;
}

bool str_equal_cstr(struct str left, const char *right) {
    return str_equal(left, str_from_cstr(right));
}

bool str_equal_ignore_case(struct str left, struct str right) {
    if (left.length != right.length) return false;
    for (size_t index = 0; index < left.length; index++) {
        if (lower(left.data[index]) != lower(right.data[index])) return false;
    }
    return true;
}

bool str_starts_with(struct str text, struct str prefix) {
    if (prefix.length > text.length) return false;
    return str_equal(str_slice(text, 0, prefix.length), prefix);
}

struct str str_trim(struct str text) {
    size_t start = 0;
    size_t end = text.length;
    while (start < end && is_space(text.data[start])) start++;
    while (end > start && is_space(text.data[end - 1])) end--;
    return str_slice(text, start, end - start);
}

bool str_to_u64(struct str text, uint64_t *out) {
    uint64_t base = DECIMAL_BASE;
    size_t index = 0;

    if (text.length == 0) return false;
    if (text.length > 2 && text.data[0] == '0' && lower(text.data[1]) == 'x') {
        base = HEX_BASE;
        index = 2;
    }
    if (index >= text.length) return false;

    uint64_t value = 0;
    for (; index < text.length; index++) {
        uint64_t digit;
        if (!digit_value(text.data[index], base, &digit)) return false;
        if (!checked_mul_u64(value, base, &value)) return false;
        if (!checked_add_u64(value, digit, &value)) return false;
    }
    *out = value;
    return true;
}

bool str_copy_cstr(struct str text, char *out, size_t capacity) {
    if (capacity == 0 || text.length >= capacity) return false;
    for (size_t index = 0; index < text.length; index++) out[index] = text.data[index];
    out[text.length] = '\0';
    return true;
}
