#include "cpu/cpuid.h"
#include "log/log.h"
#include "serial/serial.h"

#ifndef SERIAL_PORT
#define SERIAL_PORT 0x3F8U
#endif

static void report_features(const struct cpu_features *features) {
    LOG_INFO("cpu %s, %u physical / %u linear address bits",
             features->vendor,
             (uint64_t)features->physical_address_bits,
             (uint64_t)features->linear_address_bits);
    LOG_INFO("long mode %s, nx %s, 1g pages %s",
             features->long_mode ? "yes" : "no",
             features->no_execute ? "yes" : "no",
             features->gigabyte_pages ? "yes" : "no");
    LOG_INFO("smep %s, smap %s, avx %s",
             features->smep ? "yes" : "no",
             features->smap ? "yes" : "no",
             features->avx ? "yes" : "no");
}

/* Entered from stage2 with the processor already in long mode, paging on, and
   the boot drive in the first argument. */
void boot_main(uint8_t boot_drive);

void boot_main(uint8_t boot_drive) {
    if (serial_init(SERIAL_PORT)) log_set_sink(serial_write_cstr);

    LOG_INFO("tunix-boot starting from drive %x", (uint64_t)boot_drive);

    struct cpu_features features;
    cpu_features_detect(cpuid_execute, &features);
    report_features(&features);

    if (!features.long_mode) {
        LOG_ERROR("the processor does not report long mode");
        return;
    }

    LOG_INFO("stage2 reached the core");
}
