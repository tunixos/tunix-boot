#include "harness.h"
#include "util/str.h"

static void compares_by_length_not_terminator(void) {
    struct str full = str_from_cstr("kernel");
    CHECK(str_equal_cstr(full, "kernel"));
    CHECK(!str_equal_cstr(str_slice(full, 0, 3), "kernel"));
    CHECK(str_equal_cstr(str_slice(full, 0, 3), "ker"));
}

static void slicing_clamps_instead_of_running_over(void) {
    struct str text = str_from_cstr("abc");
    CHECK(str_slice(text, 1, 100).length == 2);
    CHECK(str_slice(text, 3, 1).length == 0);
    CHECK(str_slice(text, 99, 1).length == 0);
}

static void trims_both_ends(void) {
    CHECK(str_equal_cstr(str_trim(str_from_cstr("  x \t\r\n")), "x"));
    CHECK(str_trim(str_from_cstr("   ")).length == 0);
}

static void parses_decimal_and_hex(void) {
    uint64_t value = 0;
    CHECK(str_to_u64(str_from_cstr("0"), &value) && value == 0);
    CHECK(str_to_u64(str_from_cstr("4096"), &value) && value == 4096);
    CHECK(str_to_u64(str_from_cstr("0xFF"), &value) && value == 255);
    CHECK(str_to_u64(str_from_cstr("0xffffffffffffffff"), &value) &&
          value == 0xFFFFFFFFFFFFFFFFULL);
}

static void rejects_bad_numbers(void) {
    uint64_t value = 0;
    CHECK(!str_to_u64(str_from_cstr(""), &value));
    CHECK(!str_to_u64(str_from_cstr("0x"), &value));
    CHECK(!str_to_u64(str_from_cstr("12a"), &value));
    CHECK(!str_to_u64(str_from_cstr("0xg"), &value));
    /* One past the widest value it can hold. */
    CHECK(!str_to_u64(str_from_cstr("18446744073709551616"), &value));
}

static void copying_refuses_to_overflow_the_buffer(void) {
    char buffer[4];
    CHECK(str_copy_cstr(str_from_cstr("abc"), buffer, sizeof buffer));
    CHECK(buffer[3] == '\0');
    CHECK(!str_copy_cstr(str_from_cstr("abcd"), buffer, sizeof buffer));
    CHECK(!str_copy_cstr(str_from_cstr("a"), buffer, 0));
}

static void ignores_case_when_asked(void) {
    CHECK(str_equal_ignore_case(str_from_cstr("Kernel"), str_from_cstr("kERNEL")));
    CHECK(!str_equal_ignore_case(str_from_cstr("kernel"), str_from_cstr("kernels")));
}

static void detects_prefixes(void) {
    CHECK(str_starts_with(str_from_cstr("/boot/kernel"), str_from_cstr("/boot/")));
    CHECK(!str_starts_with(str_from_cstr("/bo"), str_from_cstr("/boot/")));
}

TEST_MAIN(
    compares_by_length_not_terminator();
    slicing_clamps_instead_of_running_over();
    trims_both_ends();
    parses_decimal_and_hex();
    rejects_bad_numbers();
    copying_refuses_to_overflow_the_buffer();
    ignores_case_when_asked();
    detects_prefixes();
)
