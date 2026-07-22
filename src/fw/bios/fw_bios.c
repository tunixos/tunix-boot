#include "fw/bios/e820.h"
#include "acpi/acpi.h"
#include "fw/bios/vbe.h"
#include "fw/bios/fw_bios.h"

#define FW_BIOS_NAME "bios"

/* ops first, so a struct fw_ops * can be cast back to this. */
struct fw_bios {
    struct fw_ops ops;
    uint8_t drive;
    uint32_t e820_count;
    const void *e820_buffer;
    const void *vbe_mode_info;
};

static struct fw_bios firmware;

static bool bios_mem_snapshot(const struct fw_ops *fw, struct memory_map *out) {
    const struct fw_bios *self = (const struct fw_bios *)fw;
    return e820_parse(self->e820_buffer, self->e820_count, out);
}

static bool bios_framebuffer_acquire(const struct fw_ops *fw,
                                    struct framebuffer *out) {
    const struct fw_bios *self = (const struct fw_bios *)fw;
    return vbe_describe(self->vbe_mode_info, out);
}

/* The two places the specification says an RSDP may be, in the order it says to
   look: the first kilobyte the EBDA pointer names, then the BIOS area. */
#define BIOS_EBDA_POINTER 0x40EU
#define BIOS_EBDA_SEARCH_BYTES 1024U
#define BIOS_ROM_START 0xE0000U
#define BIOS_ROM_BYTES 0x20000U

static const void *bios_rsdp_locate(const struct fw_ops *fw) {
    (void)fw;

    const uint16_t *ebda_pointer = (const uint16_t *)(uintptr_t)BIOS_EBDA_POINTER;
    uint64_t ebda = (uint64_t)*ebda_pointer << 4;
    if (ebda != 0) {
        const void *found = acpi_find_rsdp((const void *)(uintptr_t)ebda,
                                           BIOS_EBDA_SEARCH_BYTES);
        if (found) return found;
    }
    return acpi_find_rsdp((const void *)(uintptr_t)BIOS_ROM_START,
                          BIOS_ROM_BYTES);
}

const struct fw_ops *fw_bios_init(uint8_t drive, uint32_t e820_count,
                                  uint64_t e820_buffer,
                                  uint64_t vbe_mode_info) {
    firmware.ops.name = FW_BIOS_NAME;
    firmware.ops.mem_snapshot = bios_mem_snapshot;
    firmware.drive = drive;
    firmware.e820_count = e820_count;
    firmware.e820_buffer = (const void *)(uintptr_t)e820_buffer;
    firmware.vbe_mode_info = (const void *)(uintptr_t)vbe_mode_info;
    firmware.ops.framebuffer_acquire = bios_framebuffer_acquire;
    firmware.ops.rsdp_locate = bios_rsdp_locate;
    return &firmware.ops;
}
