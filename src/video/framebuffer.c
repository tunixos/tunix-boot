#include "video/framebuffer.h"
#include "util/mem.h"

#define CHANNEL_FULL_BITS 8U

uint32_t framebuffer_bytes_per_pixel(const struct framebuffer *fb) {
    return (fb->bits_per_pixel + FRAMEBUFFER_BITS_PER_BYTE - 1U) /
           FRAMEBUFFER_BITS_PER_BYTE;
}

static bool channel_valid(const struct framebuffer_channel *channel,
                          uint32_t bits_per_pixel) {
    if (channel->bits == 0 || channel->bits > FRAMEBUFFER_CHANNEL_MAX_BITS)
        return false;
    /* A channel that runs off the end of a pixel would have its top bits
       written into whatever follows. */
    return (uint32_t)channel->shift + channel->bits <= bits_per_pixel;
}

bool framebuffer_valid(const struct framebuffer *fb) {
    if (!fb || !fb->base) return false;
    if (fb->width == 0 || fb->height == 0) return false;
    if (fb->width > FRAMEBUFFER_MAX_WIDTH) return false;
    if (fb->height > FRAMEBUFFER_MAX_HEIGHT) return false;

    if (fb->bits_per_pixel < FRAMEBUFFER_MIN_BITS_PER_PIXEL) return false;
    if (fb->bits_per_pixel > FRAMEBUFFER_MAX_BITS_PER_PIXEL) return false;

    /* A pitch shorter than a row means rows overlap, and every write past the
       first would land in the row above. */
    if (fb->pitch < fb->width * framebuffer_bytes_per_pixel(fb)) return false;

    if (!channel_valid(&fb->red, fb->bits_per_pixel)) return false;
    if (!channel_valid(&fb->green, fb->bits_per_pixel)) return false;
    if (!channel_valid(&fb->blue, fb->bits_per_pixel)) return false;
    return true;
}

static uint32_t pack_channel(const struct framebuffer_channel *channel,
                             uint8_t value) {
    /* The high bits are the ones kept: dropping the low bits of 0xFF still
       gives every bit set, where masking the high ones would give zero. */
    uint32_t narrowed = (uint32_t)value >> (CHANNEL_FULL_BITS - channel->bits);
    return narrowed << channel->shift;
}

uint32_t framebuffer_pack(const struct framebuffer *fb, uint8_t red,
                          uint8_t green, uint8_t blue) {
    return pack_channel(&fb->red, red) | pack_channel(&fb->green, green) |
           pack_channel(&fb->blue, blue);
}

static uint8_t *pixel_at(const struct framebuffer *fb, uint32_t x, uint32_t y) {
    return (uint8_t *)fb->base + (size_t)y * fb->pitch +
           (size_t)x * framebuffer_bytes_per_pixel(fb);
}

void framebuffer_put(const struct framebuffer *fb, uint32_t x, uint32_t y,
                     uint32_t colour) {
    if (x >= fb->width || y >= fb->height) return;

    uint8_t *at = pixel_at(fb, x, y);
    uint32_t bytes = framebuffer_bytes_per_pixel(fb);
    for (uint32_t index = 0; index < bytes; index++) {
        at[index] = (uint8_t)(colour >> (index * FRAMEBUFFER_BITS_PER_BYTE));
    }
}

uint32_t framebuffer_get(const struct framebuffer *fb, uint32_t x, uint32_t y) {
    if (x >= fb->width || y >= fb->height) return 0;

    const uint8_t *at = pixel_at(fb, x, y);
    uint32_t bytes = framebuffer_bytes_per_pixel(fb);
    uint32_t colour = 0;
    for (uint32_t index = 0; index < bytes; index++) {
        colour |= (uint32_t)at[index] << (index * FRAMEBUFFER_BITS_PER_BYTE);
    }
    return colour;
}

void framebuffer_fill(const struct framebuffer *fb, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, uint32_t colour) {
    if (x >= fb->width || y >= fb->height) return;

    if (width > fb->width - x) width = fb->width - x;
    if (height > fb->height - y) height = fb->height - y;

    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t column = 0; column < width; column++) {
            framebuffer_put(fb, x + column, y + row, colour);
        }
    }
}

void framebuffer_move_rows(const struct framebuffer *fb, uint32_t to_y,
                           uint32_t from_y, uint32_t height) {
    if (to_y >= fb->height || from_y >= fb->height) return;
    if (to_y == from_y) return;

    uint32_t available = fb->height - (to_y > from_y ? to_y : from_y);
    if (height > available) height = available;
    if (height == 0) return;

    size_t row_bytes = (size_t)fb->width * framebuffer_bytes_per_pixel(fb);

    /* Row by row in the direction that does not overwrite what is still to be
       read. Scrolling up moves each row to a lower address, so front to back. */
    if (to_y < from_y) {
        for (uint32_t row = 0; row < height; row++) {
            memcpy(pixel_at(fb, 0, to_y + row), pixel_at(fb, 0, from_y + row),
                   row_bytes);
        }
    } else {
        for (uint32_t row = height; row > 0; row--) {
            memcpy(pixel_at(fb, 0, to_y + row - 1U),
                   pixel_at(fb, 0, from_y + row - 1U), row_bytes);
        }
    }
}
