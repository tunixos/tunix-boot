#ifndef TUNIX_BOOT_TERMINAL_TERMINAL_H
#define TUNIX_BOOT_TERMINAL_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include "video/framebuffer.h"

/*
 * Text on a framebuffer.
 *
 * The only reason this exists is that a machine which will not boot has to be
 * able to say so to someone who is looking at the screen rather than a serial
 * cable. So it is deliberately the smallest thing that can do that: a cursor,
 * a newline, and scrolling when it runs off the bottom. The colour it draws in
 * can be changed between characters, but by a call rather than by escape
 * sequences in the text — the loader knows what it is printing, and text that
 * carries its own formatting is text that can lie about it.
 *
 * The font is a parameter rather than something this file contains, which keeps
 * the glyph data out of the logic and lets the tests drive it with a font of
 * three characters they can assert every pixel of.
 */

#define TERMINAL_TAB_WIDTH 8U
#define TERMINAL_NEWLINE '\n'
#define TERMINAL_CARRIAGE_RETURN '\r'
#define TERMINAL_TAB '\t'

/*
 * Glyphs are rows of bits, most significant bit leftmost, one byte per row for
 * fonts up to eight pixels wide. `first` is the code of glyphs[0]; anything
 * outside the range is drawn as the replacement glyph so that unprintable text
 * is visible rather than invisible.
 */
struct terminal_font {
    const uint8_t *glyphs;
    uint32_t width;
    uint32_t height;
    uint32_t first;
    uint32_t count;
};

struct terminal {
    const struct framebuffer *fb;
    const struct terminal_font *font;
    uint32_t columns;
    uint32_t rows;
    uint32_t column;
    uint32_t row;
    uint32_t foreground;
    uint32_t background;
};

/* False when the framebuffer cannot hold a single character, or the font is
   not one this can draw. */
bool terminal_init(struct terminal *terminal, const struct framebuffer *fb,
                   const struct terminal_font *font, uint32_t foreground,
                   uint32_t background);

void terminal_clear(struct terminal *terminal);

/* The colour characters are drawn in from here on. The background is left
   alone: what is already on the screen keeps the colour it was written in. */
void terminal_set_foreground(struct terminal *terminal, uint32_t colour);

/* Control characters are acted on rather than drawn; everything else advances
   the cursor, wrapping and scrolling as it goes. */
void terminal_put(struct terminal *terminal, char character);

void terminal_write(struct terminal *terminal, const char *text);

#endif
