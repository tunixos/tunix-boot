#include "fw/uefi/graphics.h"
#include "util/checked.h"

#define BITS_PER_UINT32 32U

bool uefi_channel_from_mask(uint32_t mask, struct framebuffer_channel *out) {
    if (mask == 0) return false;

    uint32_t shift = 0;
    while ((mask & 1U) == 0) {
        mask >>= 1;
        shift++;
    }

    uint32_t bits = 0;
    while ((mask & 1U) != 0) {
        mask >>= 1;
        bits++;
    }

    /* Anything still set means the mask had a gap in it. No hardware reports
       one, and a channel that is not contiguous is not a shift and a width. */
    if (mask != 0) return false;
    if (bits > FRAMEBUFFER_CHANNEL_MAX_BITS) return false;
    if (shift + bits > BITS_PER_UINT32) return false;

    out->shift = (uint8_t)shift;
    out->bits = (uint8_t)bits;
    return true;
}

bool uefi_graphics_describe(uint32_t pixel_format,
                            const struct uefi_pixel_masks *masks,
                            uint32_t width, uint32_t height,
                            uint32_t pixels_per_scanline, uint64_t base,
                            struct framebuffer *out) {
    if (!out || base == 0) return false;
    if (width == 0 || height == 0) return false;

    /* Firmware reports this separately from the width, and it is never smaller.
       One that is has misdescribed the mode, and drawing on it would run each
       row into the one above. */
    if (pixels_per_scanline < width) return false;

    switch (pixel_format) {
        case UEFI_PIXEL_RGB_RESERVED_8:
            out->red = (struct framebuffer_channel){0, 8};
            out->green = (struct framebuffer_channel){8, 8};
            out->blue = (struct framebuffer_channel){16, 8};
            break;

        case UEFI_PIXEL_BGR_RESERVED_8:
            out->blue = (struct framebuffer_channel){0, 8};
            out->green = (struct framebuffer_channel){8, 8};
            out->red = (struct framebuffer_channel){16, 8};
            break;

        case UEFI_PIXEL_BIT_MASK:
            if (!masks) return false;
            if (!uefi_channel_from_mask(masks->red, &out->red)) return false;
            if (!uefi_channel_from_mask(masks->green, &out->green)) return false;
            if (!uefi_channel_from_mask(masks->blue, &out->blue)) return false;
            /* Two channels over the same bits would have each overwrite the
               other, and the picture would come out in one colour. */
            if (masks->red & masks->green) return false;
            if (masks->red & masks->blue) return false;
            if (masks->green & masks->blue) return false;
            break;

        default:
            return false;
    }

    uint64_t pitch;
    if (!checked_mul_u64(pixels_per_scanline,
                         UEFI_GRAPHICS_BITS_PER_PIXEL /
                             FRAMEBUFFER_BITS_PER_BYTE,
                         &pitch)) {
        return false;
    }
    if (pitch > UINT32_MAX) return false;

    out->base = (void *)(uintptr_t)base;
    out->width = width;
    out->height = height;
    out->pitch = (uint32_t)pitch;
    out->bits_per_pixel = UEFI_GRAPHICS_BITS_PER_PIXEL;

    return framebuffer_valid(out);
}
