#include "boot/core.h"
#include "fw/uefi/uefi.h"
#include "log/log.h"
#include "serial/serial.h"

#ifndef SERIAL_PORT
#define SERIAL_PORT 0x3F8U
#endif

const struct fw_ops *fw_uefi_init(efi_handle image,
                                  struct efi_system_table *system);

/*
 * Where the firmware hands over.
 *
 * Unlike the BIOS path there is no stage1 and no stage2: the firmware has
 * already put the processor in long mode with paging on and loaded this image
 * wherever it liked. So there is nothing to set up here — the core is written
 * against `struct fw_ops` and does not know which of the two got it here.
 *
 * Logging goes to the serial port rather than the firmware console for the same
 * reason: it keeps working after boot services end, and it is the same output
 * the BIOS build produces, so one test script reads both.
 */
efi_status efi_main(efi_handle image, struct efi_system_table *system);

efi_status efi_main(efi_handle image, struct efi_system_table *system) {
    if (serial_init(SERIAL_PORT)) log_set_sink(serial_write_cstr);

    const struct fw_ops *fw = fw_uefi_init(image, system);
    if (!fw) {
        LOG_ERROR("the firmware did not hand over a system table");
        return EFI_INVALID_PARAMETER;
    }

    boot_core(fw);

    /* boot_core only returns when it could not boot, and there is nothing
       useful left to do about that here. */
    LOG_ERROR("nothing was booted");
    for (;;) __asm__ __volatile__("hlt");
}
