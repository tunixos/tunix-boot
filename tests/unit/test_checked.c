#include "harness.h"
#include "util/checked.h"

static void addition_reports_the_wrap(void) {
    uint64_t out = 0;
    CHECK(checked_add_u64(1, 2, &out) && out == 3);
    CHECK(!checked_add_u64(0xFFFFFFFFFFFFFFFFULL, 1, &out));
}

static void multiplication_reports_the_wrap(void) {
    uint64_t out = 0;
    CHECK(checked_mul_u64(1U << 20, 1U << 20, &out) && out == (1ULL << 40));
    CHECK(!checked_mul_u64(0x8000000000000000ULL, 2, &out));
}

static void narrowing_refuses_what_does_not_fit(void) {
    size_t out = 0;
    CHECK(checked_narrow_size(4096, &out) && out == 4096);
    if (sizeof(size_t) < sizeof(uint64_t))
        CHECK(!checked_narrow_size(0xFFFFFFFFFFFFFFFFULL, &out));
}

static void alignment_requires_a_power_of_two(void) {
    uint64_t out = 0;
    CHECK(checked_align_up_u64(4097, 4096, &out) && out == 8192);
    CHECK(checked_align_up_u64(4096, 4096, &out) && out == 4096);
    CHECK(!checked_align_up_u64(1, 0, &out));
    CHECK(!checked_align_up_u64(1, 3, &out));
    CHECK(!checked_align_up_u64(0xFFFFFFFFFFFFFFFFULL, 4096, &out));
}

TEST_MAIN(
    addition_reports_the_wrap();
    multiplication_reports_the_wrap();
    narrowing_refuses_what_does_not_fit();
    alignment_requires_a_power_of_two();
)
