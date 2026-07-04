#include "cpu/cpuid.h"
#include "harness.h"
#include "util/mem.h"

/*
 * A recorded CPU: the leaves a machine reported, replayed on the host. This is
 * why issuing cpuid is separated from reading it -- none of this could be
 * tested if the interpretation called the instruction itself.
 */
struct recorded_leaf {
    uint32_t leaf;
    uint32_t subleaf;
    struct cpuid_result result;
};

static const struct recorded_leaf *recording;
static size_t recording_length;

static void replay(uint32_t leaf, uint32_t subleaf, struct cpuid_result *out) {
    for (size_t index = 0; index < recording_length; index++) {
        if (recording[index].leaf == leaf && recording[index].subleaf == subleaf) {
            *out = recording[index].result;
            return;
        }
    }
    memset(out, 0, sizeof(*out));
}

static void use(const struct recorded_leaf *leaves, size_t count) {
    recording = leaves;
    recording_length = count;
}

/* Skylake-class part: long mode, NX, SMEP/SMAP, AVX2, XSAVE enabled. */
static const struct recorded_leaf MODERN[] = {
    {0x00000000, 0, {0x00000016, 0x756E6547, 0x6C65746E, 0x49656E69}},
    {0x00000001, 0, {0x000506E3, 0x00100800, 0x7FFAFBBF, 0xBFEBFBFF}},
    {0x00000007, 0, {0x00000000, 0x029C67AF, 0x00000004, 0x00000000}},
    {0x80000000, 0, {0x80000008, 0x00000000, 0x00000000, 0x00000000}},
    {0x80000001, 0, {0x00000000, 0x00000000, 0x00000121, 0x2C100800}},
    {0x80000007, 0, {0x00000000, 0x00000000, 0x00000000, 0x00000100}},
    {0x80000008, 0, {0x00003027, 0x00000000, 0x00000000, 0x00000000}},
};

/* A part that reports no extended leaves at all. */
static const struct recorded_leaf ANCIENT[] = {
    {0x00000000, 0, {0x00000001, 0x756E6547, 0x6C65746E, 0x49656E69}},
    {0x00000001, 0, {0x00000633, 0x00000000, 0x00000000, 0x0387F9FF}},
};

static void reads_a_modern_part(void) {
    struct cpu_features features;
    use(MODERN, sizeof MODERN / sizeof MODERN[0]);
    cpu_features_detect(replay, &features);

    CHECK(features.long_mode);
    CHECK(features.no_execute);
    CHECK(features.smep);
    CHECK(features.smap);
    CHECK(features.xsave && features.osxsave);
    CHECK(features.avx && features.avx2);
    CHECK(features.invariant_tsc);
    CHECK(features.physical_address_bits == 39);
    CHECK(features.linear_address_bits == 48);
    CHECK(memcmp(features.vendor, "GenuineIntel", 12) == 0);
}

static void a_part_without_extended_leaves_claims_nothing(void) {
    struct cpu_features features;
    use(ANCIENT, sizeof ANCIENT / sizeof ANCIENT[0]);
    cpu_features_detect(replay, &features);

    CHECK(!features.long_mode);
    CHECK(!features.no_execute);
    CHECK(!features.smep);
    /* Absent leaves must not leave the address widths at zero. */
    CHECK(features.physical_address_bits == 36);
    CHECK(features.linear_address_bits == 48);
}

/* AVX without OSXSAVE means the state is not enabled, so it is not usable. */
static const struct recorded_leaf AVX_NOT_ENABLED[] = {
    {0x00000000, 0, {0x00000007, 0x756E6547, 0x6C65746E, 0x49656E69}},
    /* XSAVE (26) and AVX (28) set, OSXSAVE (27) deliberately clear. */
    {0x00000001, 0, {0x000506E3, 0x00000000, 0x14000000, 0x00000000}},
    {0x00000007, 0, {0x00000000, 0x00000020, 0x00000000, 0x00000000}},
};

static void avx_is_ignored_until_the_state_is_enabled(void) {
    struct cpu_features features;
    use(AVX_NOT_ENABLED, sizeof AVX_NOT_ENABLED / sizeof AVX_NOT_ENABLED[0]);
    cpu_features_detect(replay, &features);

    CHECK(features.xsave);
    CHECK(!features.osxsave);
    CHECK(!features.avx);
    CHECK(!features.avx2);
}

static void a_cpu_that_answers_nothing_is_survivable(void) {
    struct cpu_features features;
    use(NULL, 0);
    cpu_features_detect(replay, &features);

    CHECK(!features.long_mode);
    CHECK(features.vendor[0] == '\0');
    CHECK(features.physical_address_bits == 36);
}

TEST_MAIN(
    reads_a_modern_part();
    a_part_without_extended_leaves_claims_nothing();
    avx_is_ignored_until_the_state_is_enabled();
    a_cpu_that_answers_nothing_is_survivable();
)
