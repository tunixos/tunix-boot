#ifndef TUNIX_BOOT_VIDEO_FRAMEBUFFER_H
#define TUNIX_BOOT_VIDEO_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A linear framebuffer, however the firmware happened to describe it.
 *
 * UEFI reports two named layouts and a third that is only a set of bit masks;
 * VBE reports masks always. Rather than carry three cases everywhere, both
 * backends are reduced to masks here and nothing above this knows the
 * difference. A channel is a shift and a width, which is enough to express
 * every layout either firmware can report, including the 16-bit ones where the
 * channels are not byte-aligned.
 *
 * Nothing here allocates and nothing scrolls; this is the layer that knows
 * where a pixel is, and no more than that.
 */

#define FRAMEBUFFER_MAX_WIDTH 16384U
#define FRAMEBUFFER_MAX_HEIGHT 16384U
#define FRAMEBUFFER_MIN_BITS_PER_PIXEL 15U
#define FRAMEBUFFER_MAX_BITS_PER_PIXEL 32U
#define FRAMEBUFFER_BITS_PER_BYTE 8U
#define FRAMEBUFFER_CHANNEL_MAX_BITS 8U

struct framebuffer_channel {
    uint8_t shift;
    uint8_t bits;
};

struct framebuffer {
    void *base;
    uint32_t width;
    uint32_t height;
    /* Bytes per scanline, which is not width * bytes per pixel: firmware pads
       rows, and assuming it does not draws a sheared picture. */
    uint32_t pitch;
    uint32_t bits_per_pixel;

    struct framebuffer_channel red;
    struct framebuffer_channel green;
    struct framebuffer_channel blue;
};

/* False for anything this code would have to guess about. Checked once, so the
   drawing below can be arithmetic rather than validation. */
bool framebuffer_valid(const struct framebuffer *fb);

/* An eight-bit-per-channel colour packed into the layout the firmware uses.
   Channels narrower than eight bits keep their high bits, which is what makes
   white white on a 16-bit mode rather than dim grey. */
uint32_t framebuffer_pack(const struct framebuffer *fb, uint8_t red,
                          uint8_t green, uint8_t blue);

/* Out-of-range coordinates are ignored rather than refused: a terminal at the
   bottom of the screen would otherwise have to check every glyph twice. */
void framebuffer_put(const struct framebuffer *fb, uint32_t x, uint32_t y,
                     uint32_t colour);

uint32_t framebuffer_get(const struct framebuffer *fb, uint32_t x, uint32_t y);

/* Clipped to the framebuffer. A width or height that would run past the edge
   is shortened, not refused. */
void framebuffer_fill(const struct framebuffer *fb, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, uint32_t colour);

/* Moves `height` rows from `from_y` up to `to_y`. Used by the terminal to
   scroll, and correct when the ranges overlap, which they always do. */
void framebuffer_move_rows(const struct framebuffer *fb, uint32_t to_y,
                           uint32_t from_y, uint32_t height);

uint32_t framebuffer_bytes_per_pixel(const struct framebuffer *fb);

#endif
