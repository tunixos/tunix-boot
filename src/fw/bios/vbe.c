#include "fw/bios/vbe.h"
#include "util/reader.h"

bool vbe_describe(const void *mode_info, struct framebuffer *out) {
    if (!mode_info || !out) return false;

    struct reader reader;
    reader_init(&reader, mode_info, VBE_MODE_INFO_BYTES);

    uint16_t attributes, pitch, width, height;
    uint8_t bits_per_pixel, memory_model;
    uint32_t base;

    if (!reader_seek(&reader, VBE_ATTRIBUTES_OFFSET)) return false;
    if (!reader_u16(&reader, &attributes)) return false;
    if (!reader_seek(&reader, VBE_PITCH_OFFSET)) return false;
    if (!reader_u16(&reader, &pitch)) return false;
    if (!reader_u16(&reader, &width)) return false;
    if (!reader_u16(&reader, &height)) return false;
    if (!reader_seek(&reader, VBE_BITS_PER_PIXEL_OFFSET)) return false;
    if (!reader_u8(&reader, &bits_per_pixel)) return false;
    if (!reader_seek(&reader, VBE_MEMORY_MODEL_OFFSET)) return false;
    if (!reader_u8(&reader, &memory_model)) return false;

    if ((attributes & VBE_ATTRIBUTE_SUPPORTED) == 0) return false;
    if ((attributes & VBE_ATTRIBUTE_GRAPHICS) == 0) return false;
    if ((attributes & VBE_ATTRIBUTE_LINEAR) == 0) return false;

    /* Anything else is a palette, and a palette is a different thing to draw
       through than a set of channel masks. */
    if (memory_model != VBE_MEMORY_MODEL_DIRECT_COLOUR) return false;

    struct framebuffer_channel red, green, blue;
    if (!reader_seek(&reader, VBE_RED_BITS_OFFSET)) return false;
    if (!reader_u8(&reader, &red.bits)) return false;
    if (!reader_u8(&reader, &red.shift)) return false;
    if (!reader_u8(&reader, &green.bits)) return false;
    if (!reader_u8(&reader, &green.shift)) return false;
    if (!reader_u8(&reader, &blue.bits)) return false;
    if (!reader_u8(&reader, &blue.shift)) return false;

    if (!reader_seek(&reader, VBE_FRAMEBUFFER_OFFSET)) return false;
    if (!reader_u32(&reader, &base)) return false;
    if (base == 0) return false;

    out->base = (void *)(uintptr_t)base;
    out->width = width;
    out->height = height;
    out->pitch = pitch;
    out->bits_per_pixel = bits_per_pixel;
    out->red = red;
    out->green = green;
    out->blue = blue;

    /* One check for everything the layout has to satisfy — a pitch shorter than
       a row, a channel running off the end of a pixel, a size of zero — rather
       than repeating it here. */
    return framebuffer_valid(out);
}
