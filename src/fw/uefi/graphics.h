#ifndef TUNIX_BOOT_FW_UEFI_GRAPHICS_H
#define TUNIX_BOOT_FW_UEFI_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "video/framebuffer.h"

/*
 * What the Graphics Output Protocol says a mode is, turned into a framebuffer.
 *
 * GOP describes a mode three different ways: two named layouts whose channel
 * positions are implied, and a third that gives bit masks. Working out which
 * is which is arithmetic on values the firmware supplied, so it is here, apart
 * from the call that fetches them, and tested against modes no machine to hand
 * actually has.
 */

#define UEFI_PIXEL_RGB_RESERVED_8 0U
#define UEFI_PIXEL_BGR_RESERVED_8 1U
#define UEFI_PIXEL_BIT_MASK 2U
/* A mode with no linear framebuffer at all — the firmware will copy rectangles
   for you and nothing else. There is nothing to draw on, so it is refused. */
#define UEFI_PIXEL_BLT_ONLY 3U

#define UEFI_GRAPHICS_BITS_PER_PIXEL 32U

struct uefi_pixel_masks {
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t reserved;
};

/* `pixels_per_scanline` is a count of pixels, not bytes, and is what gives the
   pitch — a mode whose rows are padded reports more of them than the width. */
bool uefi_graphics_describe(uint32_t pixel_format,
                            const struct uefi_pixel_masks *masks,
                            uint32_t width, uint32_t height,
                            uint32_t pixels_per_scanline, uint64_t base,
                            struct framebuffer *out);

/* A contiguous mask turned into the shift and width the framebuffer wants.
   False for a mask with a hole in it, which no hardware produces and which
   nothing below could draw through. */
bool uefi_channel_from_mask(uint32_t mask, struct framebuffer_channel *out);

#endif
