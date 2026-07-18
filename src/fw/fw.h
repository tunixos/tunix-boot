#ifndef TUNIX_BOOT_FW_FW_H
#define TUNIX_BOOT_FW_FW_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/map.h"
#include "video/framebuffer.h"

/*
 * The seam between the loader and the firmware under it.
 *
 * BIOS and UEFI agree on almost nothing, and the parts of a loader worth
 * testing — parsing, layout, relocation, handoff — care about none of the
 * differences. So the two backends are chosen at compile time, produce two
 * artefacts, and everything above this header is written once against the
 * interface. The tests supply their own implementation of it, which is the
 * point: the core is exercised on the host with no firmware at all.
 *
 * Two details are deliberately visible here rather than hidden:
 *
 *  - mem_snapshot is a snapshot. Under UEFI any allocation invalidates the map,
 *    so it may be used for planning and never as the map handed to the kernel.
 *    exit_firmware returns the one that is real.
 *  - The framebuffer must be acquired before boot services end, so acquiring it
 *    cannot be deferred to the point where the kernel is being handed its
 *    parameters. That ordering is the caller's to honour.
 */

struct fw_ops {
    const char *name;

    /* Physical memory as the firmware currently describes it. Already
       finalized: sorted, disjoint, and conservative about overlaps. */
    bool (*mem_snapshot)(const struct fw_ops *fw, struct memory_map *out);

    /* Sectors from the device the loader was started from. */
    bool (*block_read)(const struct fw_ops *fw, uint64_t lba, uint32_t sectors,
                       void *out);

    void (*console_write)(const struct fw_ops *fw, const char *text);

    /* The screen, if there is one. Must be called before boot services end —
       under UEFI the protocol that describes it goes away with them — which is
       why this is a separate call and not part of the memory snapshot. */
    bool (*framebuffer_acquire)(const struct fw_ops *fw,
                                struct framebuffer *out);
};

/* Call through these rather than the pointers. A backend leaves unimplemented
   operations NULL, and a missing one has to read as failure at every call site
   rather than at whichever one forgot to check. */
bool fw_mem_snapshot(const struct fw_ops *fw, struct memory_map *out);
bool fw_block_read(const struct fw_ops *fw, uint64_t lba, uint32_t sectors,
                   void *out);
void fw_console_write(const struct fw_ops *fw, const char *text);

bool fw_framebuffer_acquire(const struct fw_ops *fw, struct framebuffer *out);

const char *fw_name(const struct fw_ops *fw);

#endif
