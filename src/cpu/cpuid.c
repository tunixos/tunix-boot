#include "cpu/cpuid.h"
#include "util/mem.h"

#define LEAF_VENDOR 0x00000000U
#define LEAF_FEATURES 0x00000001U
#define LEAF_EXTENDED_FEATURES 0x00000007U
#define LEAF_EXTENDED_STATE 0x0000000DU
#define LEAF_EXTENDED_MAX 0x80000000U
#define LEAF_EXTENDED_FEATURE_BITS 0x80000001U
#define LEAF_ADDRESS_SIZES 0x80000008U
#define LEAF_INVARIANT_TSC 0x80000007U

#define FEATURE_ECX_XSAVE (1U << 26)
#define FEATURE_ECX_OSXSAVE (1U << 27)
#define FEATURE_ECX_AVX (1U << 28)
#define FEATURE_ECX_RDRAND (1U << 30)
#define FEATURE_ECX_PCID (1U << 17)

#define EXTENDED_EBX_SMEP (1U << 7)
#define EXTENDED_EBX_AVX2 (1U << 5)
#define EXTENDED_EBX_SMAP (1U << 20)
#define EXTENDED_EBX_RDSEED (1U << 18)
#define EXTENDED_ECX_UMIP (1U << 2)

#define EXTENDED_FEATURE_EDX_NX (1U << 20)
#define EXTENDED_FEATURE_EDX_GBPAGES (1U << 26)
#define EXTENDED_FEATURE_EDX_LONG_MODE (1U << 29)

#define INVARIANT_TSC_EDX (1U << 8)

#define ADDRESS_BITS_MASK 0xFFU
#define LINEAR_BITS_SHIFT 8U

#define DEFAULT_PHYSICAL_ADDRESS_BITS 36U
#define DEFAULT_LINEAR_ADDRESS_BITS 48U

#define VENDOR_REGISTER_BYTES 4U

void cpuid_execute(uint32_t leaf, uint32_t subleaf, struct cpuid_result *out) {
    __asm__ volatile("cpuid"
                     : "=a"(out->eax), "=b"(out->ebx),
                       "=c"(out->ecx), "=d"(out->edx)
                     : "a"(leaf), "c"(subleaf));
}

static void store_vendor_register(char *out, uint32_t value) {
    for (unsigned index = 0; index < VENDOR_REGISTER_BYTES; index++)
        out[index] = (char)((value >> (index * 8U)) & 0xFFU);
}

void cpu_features_detect(cpuid_query query, struct cpu_features *out) {
    struct cpuid_result result;
    memset(out, 0, sizeof(*out));

    query(LEAF_VENDOR, 0, &result);
    uint32_t highest_basic_leaf = result.eax;
    store_vendor_register(out->vendor + 0, result.ebx);
    store_vendor_register(out->vendor + 4, result.edx);
    store_vendor_register(out->vendor + 8, result.ecx);
    out->vendor[12] = '\0';

    if (highest_basic_leaf >= LEAF_FEATURES) {
        query(LEAF_FEATURES, 0, &result);
        out->xsave = (result.ecx & FEATURE_ECX_XSAVE) != 0;
        out->osxsave = (result.ecx & FEATURE_ECX_OSXSAVE) != 0;
        out->avx = (result.ecx & FEATURE_ECX_AVX) != 0;
        out->rdrand = (result.ecx & FEATURE_ECX_RDRAND) != 0;
        out->pcid = (result.ecx & FEATURE_ECX_PCID) != 0;
    }

    if (highest_basic_leaf >= LEAF_EXTENDED_FEATURES) {
        query(LEAF_EXTENDED_FEATURES, 0, &result);
        out->smep = (result.ebx & EXTENDED_EBX_SMEP) != 0;
        out->smap = (result.ebx & EXTENDED_EBX_SMAP) != 0;
        out->avx2 = (result.ebx & EXTENDED_EBX_AVX2) != 0;
        out->rdseed = (result.ebx & EXTENDED_EBX_RDSEED) != 0;
        out->umip = (result.ecx & EXTENDED_ECX_UMIP) != 0;
    }

    /*
     * AVX state is only usable once the operating system has enabled it in
     * XCR0, which is what OSXSAVE reports. Announcing AVX the firmware has not
     * turned on would have the loader use registers it cannot save.
     */
    if (!out->osxsave) {
        out->avx = false;
        out->avx2 = false;
    }

    query(LEAF_EXTENDED_MAX, 0, &result);
    uint32_t highest_extended_leaf = result.eax;

    out->physical_address_bits = DEFAULT_PHYSICAL_ADDRESS_BITS;
    out->linear_address_bits = DEFAULT_LINEAR_ADDRESS_BITS;

    if (highest_extended_leaf >= LEAF_EXTENDED_FEATURE_BITS) {
        query(LEAF_EXTENDED_FEATURE_BITS, 0, &result);
        out->long_mode = (result.edx & EXTENDED_FEATURE_EDX_LONG_MODE) != 0;
        out->no_execute = (result.edx & EXTENDED_FEATURE_EDX_NX) != 0;
        out->gigabyte_pages = (result.edx & EXTENDED_FEATURE_EDX_GBPAGES) != 0;
    }

    if (highest_extended_leaf >= LEAF_INVARIANT_TSC) {
        query(LEAF_INVARIANT_TSC, 0, &result);
        out->invariant_tsc = (result.edx & INVARIANT_TSC_EDX) != 0;
    }

    if (highest_extended_leaf >= LEAF_ADDRESS_SIZES) {
        query(LEAF_ADDRESS_SIZES, 0, &result);
        uint8_t physical = (uint8_t)(result.eax & ADDRESS_BITS_MASK);
        uint8_t linear = (uint8_t)((result.eax >> LINEAR_BITS_SHIFT) & ADDRESS_BITS_MASK);
        if (physical) out->physical_address_bits = physical;
        if (linear) out->linear_address_bits = linear;
    }

    (void)LEAF_EXTENDED_STATE;
}
