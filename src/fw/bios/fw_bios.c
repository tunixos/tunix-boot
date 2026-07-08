#include "fw/bios/e820.h"
#include "fw/bios/fw_bios.h"

#define FW_BIOS_NAME "bios"

/* ops first, so a struct fw_ops * can be cast back to this. */
struct fw_bios {
    struct fw_ops ops;
    uint8_t drive;
    uint32_t e820_count;
    const void *e820_buffer;
};

static struct fw_bios firmware;

static bool bios_mem_snapshot(const struct fw_ops *fw, struct memory_map *out) {
    const struct fw_bios *self = (const struct fw_bios *)fw;
    return e820_parse(self->e820_buffer, self->e820_count, out);
}

const struct fw_ops *fw_bios_init(uint8_t drive, uint32_t e820_count,
                                  uint64_t e820_buffer) {
    firmware.ops.name = FW_BIOS_NAME;
    firmware.ops.mem_snapshot = bios_mem_snapshot;
    firmware.drive = drive;
    firmware.e820_count = e820_count;
    firmware.e820_buffer = (const void *)(uintptr_t)e820_buffer;
    return &firmware.ops;
}
