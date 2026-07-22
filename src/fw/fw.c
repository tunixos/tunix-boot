#include "fw/fw.h"

#define FW_NAME_UNKNOWN "unknown"

bool fw_mem_snapshot(const struct fw_ops *fw, struct memory_map *out) {
    if (!fw || !fw->mem_snapshot) return false;
    memory_map_clear(out);
    return fw->mem_snapshot(fw, out);
}

bool fw_block_read(const struct fw_ops *fw, uint64_t lba, uint32_t sectors,
                   void *out) {
    if (!fw || !fw->block_read) return false;
    if (sectors == 0 || !out) return false;
    return fw->block_read(fw, lba, sectors, out);
}

void fw_console_write(const struct fw_ops *fw, const char *text) {
    if (!fw || !fw->console_write || !text) return;
    fw->console_write(fw, text);
}

const char *fw_name(const struct fw_ops *fw) {
    if (!fw || !fw->name) return FW_NAME_UNKNOWN;
    return fw->name;
}

bool fw_framebuffer_acquire(const struct fw_ops *fw, struct framebuffer *out) {
    if (!fw || !fw->framebuffer_acquire || !out) return false;
    if (!fw->framebuffer_acquire(fw, out)) return false;
    /* A backend that describes a screen wrongly is worse than one that reports
       none, so the description is checked here rather than trusted. */
    return framebuffer_valid(out);
}

const void *fw_rsdp_locate(const struct fw_ops *fw) {
    if (!fw || !fw->rsdp_locate) return NULL;
    return fw->rsdp_locate(fw);
}
