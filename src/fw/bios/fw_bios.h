#ifndef TUNIX_BOOT_FW_BIOS_FW_BIOS_H
#define TUNIX_BOOT_FW_BIOS_FW_BIOS_H

#include <stdint.h>

#include "fw/fw.h"

/* Wraps what stage2 gathered before it left real mode. The returned pointer is
   to storage that outlives the call; there is one firmware and one loader. */
const struct fw_ops *fw_bios_init(uint8_t drive, uint32_t e820_count,
                                  uint64_t e820_buffer);

#endif
