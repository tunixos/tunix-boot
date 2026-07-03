#ifndef TUNIX_BOOT_CPU_CPUID_H
#define TUNIX_BOOT_CPU_CPUID_H

#include <stdbool.h>
#include <stdint.h>

struct cpuid_result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

/*
 * Issuing the instruction is separated from interpreting what it returns, so
 * the interpretation can be tested on the build host against a table of
 * recorded CPUs instead of only on whatever machine happens to run the tests.
 */
typedef void (*cpuid_query)(uint32_t leaf, uint32_t subleaf,
                            struct cpuid_result *out);

void cpuid_execute(uint32_t leaf, uint32_t subleaf, struct cpuid_result *out);

struct cpu_features {
    bool long_mode;
    bool no_execute;
    bool gigabyte_pages;
    bool pcid;
    bool smep;
    bool smap;
    bool umip;
    bool xsave;
    bool osxsave;
    bool avx;
    bool avx2;
    bool rdrand;
    bool rdseed;
    bool invariant_tsc;
    uint8_t physical_address_bits;
    uint8_t linear_address_bits;
    char vendor[13];
};

/* Fills `out` from the leaves `query` reports. Absent leaves read as zero, so a
   CPU that predates a leaf simply lacks the features it would have announced. */
void cpu_features_detect(cpuid_query query, struct cpu_features *out);

#endif
