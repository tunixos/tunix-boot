#include "harness.h"
#include "terminal/font8x16.h"

/* The font is data, so what is worth testing is that the table lines up with
   the range it claims: an entry too many or too few shifts every glyph after it
   and the screen fills with the wrong letters. */
static const struct terminal_font *font;

/* Everything from here down hangs below the baseline. */
#define DESCENDER_FIRST_ROW 13U

static const uint8_t *glyph(char character) {
    return font->glyphs + ((size_t)(unsigned char)character - font->first) *
                              font->height;
}

static bool hangs_below_the_baseline(char character) {
    for (unsigned row = DESCENDER_FIRST_ROW; row < FONT8X16_HEIGHT; row++)
        if (glyph(character)[row] != 0) return true;
    return false;
}

static void describes_itself_consistently(void) {
    font = font8x16();
    CHECK(font != NULL && font->glyphs != NULL);
    CHECK(font->width == FONT8X16_WIDTH);
    CHECK(font->height == FONT8X16_HEIGHT);
    CHECK(font->first == ' ');
    CHECK(font->count == FONT8X16_COUNT);
    /* Every printable character, and exactly those. */
    CHECK(font->first + font->count - 1U == '~');
}

static void the_table_is_the_length_it_claims(void) {
    font = font8x16();
    /* The last glyph must be the one '~' is drawn from. If the table is short
       by an entry this reads past it; if long, every glyph after the mistake is
       the wrong one. */
    const uint8_t *last = glyph('~');
    CHECK(last[6] == 0x72 && last[7] == 0x52 && last[8] == 0x4C);
    for (unsigned row = 0; row < 6U; row++) CHECK(last[row] == 0);
    for (unsigned row = 9U; row < FONT8X16_HEIGHT; row++) CHECK(last[row] == 0);
}

static void a_space_is_blank_and_a_rule_is_not(void) {
    font = font8x16();
    for (unsigned row = 0; row < FONT8X16_HEIGHT; row++)
        CHECK(glyph(' ')[row] == 0);

    /* An underscore is the one glyph that is a single row and nothing else. */
    const uint8_t *underscore = glyph('_');
    for (unsigned row = 0; row < FONT8X16_HEIGHT; row++)
        CHECK(underscore[row] == (row == DESCENDER_FIRST_ROW ? 0x7E : 0x00));
}

/* Spot checks at both ends and the middle, so an off-by-one anywhere in the
   table moves at least one of them. */
static void known_glyphs_are_where_they_should_be(void) {
    font = font8x16();
    CHECK(glyph('!')[3] == 0x18 && glyph('!')[12] == 0x18);
    CHECK(glyph('0')[3] == 0x3C && glyph('0')[7] == 0x5A);
    CHECK(glyph('A')[3] == 0x18 && glyph('A')[9] == 0x3C);
    CHECK(glyph('I')[3] == 0x7C && glyph('I')[12] == 0x7C);
    CHECK(glyph('a')[5] == 0x3C && glyph('a')[12] == 0x3E);
    CHECK(glyph('|')[1] == 0x10 && glyph('|')[14] == 0x10);
}

static void descenders_are_the_only_thing_below_the_baseline(void) {
    font = font8x16();
    /* The letters that hang below the baseline are the ones a font rendered
       onto a grid this small usually loses the tail of. */
    CHECK(hangs_below_the_baseline('g'));
    CHECK(hangs_below_the_baseline('p'));
    CHECK(hangs_below_the_baseline('q'));
    CHECK(hangs_below_the_baseline('y'));
    CHECK(hangs_below_the_baseline('j'));
    CHECK(hangs_below_the_baseline(','));

    /* And the ones that do not, do not: a letter that reaches into the row
       below is one that touches the line under it. */
    CHECK(!hangs_below_the_baseline('o'));
    CHECK(!hangs_below_the_baseline('A'));
    CHECK(!hangs_below_the_baseline('x'));
}

static void no_glyph_is_entirely_blank_except_space(void) {
    font = font8x16();
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
    a_space_is_blank_and_a_rule_is_not();
    known_glyphs_are_where_they_should_be();
    descenders_are_the_only_thing_below_the_baseline();
    no_glyph_is_entirely_blank_except_space();
)
