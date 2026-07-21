#include "harness.h"
#include "terminal/font8x8.h"

/* The font is data, so what is worth testing is that the table lines up with
   the range it claims: an entry too many or too few shifts every glyph after it
   and the screen fills with the wrong letters. */
static const struct terminal_font *font;

static const uint8_t *glyph(char character) {
    return font->glyphs + ((size_t)(unsigned char)character - font->first) *
                              font->height;
}

static void describes_itself_consistently(void) {
    font = font8x8();
    CHECK(font != NULL && font->glyphs != NULL);
    CHECK(font->width == FONT8X8_WIDTH);
    CHECK(font->height == FONT8X8_HEIGHT);
    CHECK(font->first == ' ');
    CHECK(font->count == FONT8X8_COUNT);
    /* Every printable character, and exactly those. */
    CHECK(font->first + font->count - 1U == '~');
}

static void the_table_is_the_length_it_claims(void) {
    font = font8x8();
    /* The last glyph must be the one '~' is drawn from. If the table is short
       by an entry this reads past it; if long, every glyph after the mistake is
       the wrong one. */
    const uint8_t *last = glyph('~');
    CHECK(last[0] == 0x76 && last[1] == 0xDC);
    for (unsigned row = 2; row < FONT8X8_HEIGHT; row++) CHECK(last[row] == 0);
}

static void a_space_is_blank_and_a_block_is_not(void) {
    font = font8x8();
    for (unsigned row = 0; row < FONT8X8_HEIGHT; row++)
        CHECK(glyph(' ')[row] == 0);

    /* An underscore is the one glyph that is only its bottom row. */
    const uint8_t *underscore = glyph('_');
    for (unsigned row = 0; row < FONT8X8_HEIGHT - 1U; row++)
        CHECK(underscore[row] == 0);
    CHECK(underscore[FONT8X8_HEIGHT - 1U] == 0xFF);
}

/* Spot checks at both ends and the middle, so an off-by-one anywhere in the
   table moves at least one of them. */
static void known_glyphs_are_where_they_should_be(void) {
    font = font8x8();
    CHECK(glyph('!')[0] == 0x18);
    CHECK(glyph('0')[0] == 0x3C);
    CHECK(glyph('A')[0] == 0x18 && glyph('A')[4] == 0x7E);
    CHECK(glyph('I')[0] == 0x7E && glyph('I')[6] == 0x7E);
    CHECK(glyph('a')[0] == 0x00 && glyph('a')[2] == 0x3C);
    CHECK(glyph('|')[0] == 0x18 && glyph('|')[6] == 0x18);
}

static void descenders_use_the_last_row(void) {
    font = font8x8();
    /* The letters that hang below the baseline are the ones a font drawn on a
       grid this small usually gets wrong. */
    CHECK(glyph('g')[FONT8X8_HEIGHT - 1U] != 0);
    CHECK(glyph('p')[FONT8X8_HEIGHT - 1U] != 0);
    CHECK(glyph('q')[FONT8X8_HEIGHT - 1U] != 0);
    CHECK(glyph('y')[FONT8X8_HEIGHT - 1U] != 0);
    CHECK(glyph('j')[FONT8X8_HEIGHT - 1U] != 0);
    CHECK(glyph(',')[FONT8X8_HEIGHT - 1U] != 0);

    /* And the ones that do not, do not. */
    CHECK(glyph('o')[FONT8X8_HEIGHT - 1U] == 0);
    CHECK(glyph('A')[FONT8X8_HEIGHT - 1U] == 0);
}

static void no_glyph_is_entirely_blank_except_space(void) {
    font = font8x8();
    for (unsigned index = 1; index < font->count; index++) {
        const uint8_t *rows = font->glyphs + (size_t)index * font->height;
        bool any = false;
        for (unsigned row = 0; row < font->height; row++)
            if (rows[row] != 0) any = true;
        /* A blank glyph in the middle of the table is what a miscounted entry
           looks like, and it is invisible on screen. */
        CHECK(any);
    }
}

TEST_MAIN(
    describes_itself_consistently();
    the_table_is_the_length_it_claims();
    a_space_is_blank_and_a_block_is_not();
    known_glyphs_are_where_they_should_be();
    descenders_use_the_last_row();
    no_glyph_is_entirely_blank_except_space();
)
