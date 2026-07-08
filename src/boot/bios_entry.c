#include "boot/core.h"
#include "fw/bios/fw_bios.h"
#include "log/log.h"
#include "serial/serial.h"

#ifndef SERIAL_PORT
#define SERIAL_PORT 0x3F8U
#endif

/* Entered from stage2 with the processor in long mode, paging on, .bss cleared,
   and the E820 entries it collected in real mode waiting at e820_buffer. */
void boot_main(uint8_t boot_drive, uint32_t e820_count, uint64_t e820_buffer);

void boot_main(uint8_t boot_drive, uint32_t e820_count, uint64_t e820_buffer) {
    if (serial_init(SERIAL_PORT)) log_set_sink(serial_write_cstr);

    LOG_INFO("boot drive %x, %u firmware memory entries",
             (uint64_t)boot_drive, (uint64_t)e820_count);

    boot_core(fw_bios_init(boot_drive, e820_count, e820_buffer));
}
