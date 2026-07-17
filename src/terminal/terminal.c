#include "terminal/terminal.h"

#define GLYPH_MAX_WIDTH 8U
#define GLYPH_LEFTMOST_BIT 0x80U
/* Drawn in place of anything the font does not have, so text that cannot be
   rendered is visible rather than absent. */
#define REPLACEMENT_FILLED true

bool terminal_init(struct terminal *terminal, const struct framebuffer *fb,
                   const struct terminal_font *font, uint32_t foreground,
                   uint32_t background) {
    if (!terminal || !font || !font->glyphs) return false;
    if (!framebuffer_valid(fb)) return false;
    if (font->width == 0 || font->width > GLYPH_MAX_WIDTH) return false;
    if (font->height == 0 || font->count == 0) return false;
    if (fb->width < font->width || fb->height < font->height) return false;

    terminal->fb = fb;
    terminal->font = font;
    terminal->columns = fb->width / font->width;
    terminal->rows = fb->height / font->height;
    terminal->column = 0;
    terminal->row = 0;
    terminal->foreground = foreground;
    terminal->background = background;
    return true;
}

void terminal_clear(struct terminal *terminal) {
    framebuffer_fill(terminal->fb, 0, 0, terminal->fb->width,
                     terminal->fb->height, terminal->background);
    terminal->column = 0;
    terminal->row = 0;
}

/* The rows of one glyph, or NULL when the font has no such character. */
static const uint8_t *glyph_of(const struct terminal_font *font,
                               unsigned char character) {
    if (character < font->first) return NULL;
    uint32_t index = (uint32_t)character - font->first;
    if (index >= font->count) return NULL;
    return font->glyphs + (size_t)index * font->height;
}

static void draw_glyph(const struct terminal *terminal, unsigned char character,
                       uint32_t x, uint32_t y) {
    const struct terminal_font *font = terminal->font;
    const uint8_t *glyph = glyph_of(font, character);

    for (uint32_t row = 0; row < font->height; row++) {
        uint8_t bits = glyph ? glyph[row] : 0;
        for (uint32_t column = 0; column < font->width; column++) {
            bool set = glyph ? (bits & (GLYPH_LEFTMOST_BIT >> column)) != 0
                             : REPLACEMENT_FILLED;
            framebuffer_put(terminal->fb, x + column, y + row,
                            set ? terminal->foreground : terminal->background);
        }
    }
}

/* Everything moves up one line and the line uncovered at the bottom is cleared,
   because move_rows leaves what it read from where it was. */
static void scroll(struct terminal *terminal) {
    uint32_t line = terminal->font->height;
    uint32_t kept = terminal->rows > 0 ? (terminal->rows - 1U) * line : 0;

    framebuffer_move_rows(terminal->fb, 0, line, kept);
    framebuffer_fill(terminal->fb, 0, kept, terminal->fb->width, line,
                     terminal->background);
    terminal->row = terminal->rows > 0 ? terminal->rows - 1U : 0;
}

static void newline(struct terminal *terminal) {
    terminal->column = 0;
    terminal->row++;
    if (terminal->row >= terminal->rows) scroll(terminal);
}

void terminal_put(struct terminal *terminal, char character) {
    switch (character) {
        case TERMINAL_NEWLINE:
            newline(terminal);
            return;
        case TERMINAL_CARRIAGE_RETURN:
            terminal->column = 0;
            return;
        case TERMINAL_TAB:
            /* To the next stop, and to the next line if that is past the edge:
                a tab must never leave the cursor outside the screen. */
            terminal->column =
                (terminal->column / TERMINAL_TAB_WIDTH + 1U) * TERMINAL_TAB_WIDTH;
            if (terminal->column >= terminal->columns) newline(terminal);
            return;
        default:
            break;
    }

    if (terminal->column >= terminal->columns) newline(terminal);

    draw_glyph(terminal, (unsigned char)character,
               terminal->column * terminal->font->width,
               terminal->row * terminal->font->height);

    terminal->column++;
}

void terminal_write(struct terminal *terminal, const char *text) {
    if (!text) return;
    for (size_t index = 0; text[index] != '\0'; index++) {
        terminal_put(terminal, text[index]);
    }
}
