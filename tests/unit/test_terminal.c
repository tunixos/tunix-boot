#include "harness.h"
#include "terminal/terminal.h"
#include "util/mem.h"

#define GLYPH_WIDTH 4U
#define GLYPH_HEIGHT 2U
#define COLUMNS 5U
#define ROWS 3U
#define WIDTH (COLUMNS * GLYPH_WIDTH)
#define HEIGHT (ROWS * GLYPH_HEIGHT)
#define PITCH (WIDTH * 4U + 8U)

#define FOREGROUND 0x00FFFFFFU
#define BACKGROUND 0x00000000U
#define OTHER_FOREGROUND 0x0000FF00U

/*
 * Three characters, four pixels wide and two rows tall, chosen so every pixel
 * of every glyph can be asserted. 'A' is the top row, 'B' the bottom, 'C' the
 * left column — anything else the font does not have.
 */
static const uint8_t glyphs[] = {
    0xF0, 0x00,   /* A */
    0x00, 0xF0,   /* B */
    0x80, 0x80,   /* C */
};

static const struct terminal_font font = {
    .glyphs = glyphs,
    .width = GLYPH_WIDTH,
    .height = GLYPH_HEIGHT,
    .first = 'A',
    .count = 3,
};

static uint8_t pixels[PITCH * HEIGHT];
static struct framebuffer fb;
static struct terminal terminal;

static void setup(void) {
    memset(pixels, 0, sizeof pixels);
    fb.base = pixels;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.pitch = PITCH;
    fb.bits_per_pixel = 32;
    fb.red = (struct framebuffer_channel){16, 8};
    fb.green = (struct framebuffer_channel){8, 8};
    fb.blue = (struct framebuffer_channel){0, 8};

    CHECK(terminal_init(&terminal, &fb, &font, FOREGROUND, BACKGROUND));
}

static bool lit(uint32_t x, uint32_t y) {
    return framebuffer_get(&fb, x, y) == FOREGROUND;
}

/* The cell at (column, row) as a string of the pixels that are lit. */
static bool cell_is(uint32_t column, uint32_t row, const char *expected) {
    size_t index = 0;
    for (uint32_t y = 0; y < GLYPH_HEIGHT; y++) {
        for (uint32_t x = 0; x < GLYPH_WIDTH; x++) {
            bool want = expected[index++] == '#';
            if (lit(column * GLYPH_WIDTH + x, row * GLYPH_HEIGHT + y) != want)
                return false;
        }
    }
    return true;
}

static void works_out_its_own_size(void) {
    setup();
    CHECK(terminal.columns == COLUMNS);
    CHECK(terminal.rows == ROWS);
    CHECK(terminal.column == 0 && terminal.row == 0);
}

static void refuses_what_it_cannot_draw_on(void) {
    struct terminal other;
    setup();

    struct framebuffer tiny = fb;
    tiny.width = GLYPH_WIDTH - 1U;
    CHECK(!terminal_init(&other, &tiny, &font, FOREGROUND, BACKGROUND));

    tiny = fb;
    tiny.height = GLYPH_HEIGHT - 1U;
    CHECK(!terminal_init(&other, &tiny, &font, FOREGROUND, BACKGROUND));

    struct terminal_font broken = font;
    broken.glyphs = NULL;
    CHECK(!terminal_init(&other, &fb, &broken, FOREGROUND, BACKGROUND));

    /* Wider than a byte of glyph row can describe. */
    broken = font;
    broken.width = 9;
    CHECK(!terminal_init(&other, &fb, &broken, FOREGROUND, BACKGROUND));

    broken = font;
    broken.count = 0;
    CHECK(!terminal_init(&other, &fb, &broken, FOREGROUND, BACKGROUND));
}

static void draws_a_character_where_the_cursor_is(void) {
    setup();
    terminal_put(&terminal, 'A');

    CHECK(cell_is(0, 0, "####"
                        "...."));
    CHECK(terminal.column == 1 && terminal.row == 0);

    terminal_put(&terminal, 'B');
    CHECK(cell_is(1, 0, "...."
                        "####"));
    CHECK(terminal.column == 2);
}

static void a_character_the_font_lacks_is_drawn_as_a_block(void) {
    setup();
    terminal_put(&terminal, 'Z');
    /* Filled rather than blank: text that cannot be rendered must be visible,
       because a blank screen looks like a machine that printed nothing. */
    CHECK(cell_is(0, 0, "####"
                        "####"));

    terminal_put(&terminal, 1);
    CHECK(cell_is(1, 0, "####"
                        "####"));
}

static void a_glyph_paints_its_own_background(void) {
    setup();
    framebuffer_fill(&fb, 0, 0, WIDTH, HEIGHT, FOREGROUND);

    terminal_put(&terminal, 'C');
    /* The pixels the glyph does not set must be cleared, or a character
       overwritten by a shorter one leaves the old one showing through. */
    CHECK(cell_is(0, 0, "#..."
                        "#..."));
}

static void the_colour_changes_between_characters(void) {
    setup();
    terminal_put(&terminal, 'A');
    terminal_set_foreground(&terminal, OTHER_FOREGROUND);
    terminal_put(&terminal, 'A');

    /* What is already on the screen keeps the colour it was written in, which
       is the whole point: a line is one colour up to where it changed. */
    CHECK(framebuffer_get(&fb, 0, 0) == FOREGROUND);
    CHECK(framebuffer_get(&fb, GLYPH_WIDTH, 0) == OTHER_FOREGROUND);
    /* The background is not what changed. */
    CHECK(framebuffer_get(&fb, GLYPH_WIDTH, 1) == BACKGROUND);
}

static void a_newline_moves_to_the_start_of_the_next_line(void) {
    setup();
    terminal_put(&terminal, 'A');
    terminal_put(&terminal, '\n');
    CHECK(terminal.column == 0 && terminal.row == 1);

    terminal_put(&terminal, 'B');
    CHECK(cell_is(0, 1, "...."
                        "####"));
}

static void a_carriage_return_stays_on_the_line(void) {
    setup();
    terminal_write(&terminal, "AA\rB");
    CHECK(terminal.row == 0);
    /* The second A is still there; only the first was overwritten. */
    CHECK(cell_is(0, 0, "...."
                        "####"));
    CHECK(cell_is(1, 0, "####"
                        "...."));
}

static void a_tab_moves_to_the_next_stop(void) {
    setup();
    terminal_put(&terminal, 'A');
    terminal_put(&terminal, '\t');
    /* The next multiple of eight is past the five columns there are, so it
       wraps rather than leaving the cursor off the screen. */
    CHECK(terminal.column == 0);
    CHECK(terminal.row == 1);
}

static void text_wraps_at_the_edge(void) {
    setup();
    terminal_write(&terminal, "AAAAA");
    CHECK(terminal.row == 0 && terminal.column == COLUMNS);

    /* The wrap happens when the next character arrives, not when the last one
       filled the line, so a line that ends exactly at the edge does not leave
       a blank line behind it. */
    terminal_put(&terminal, 'B');
    CHECK(terminal.row == 1 && terminal.column == 1);
    CHECK(cell_is(0, 1, "...."
                        "####"));
}

static void the_screen_scrolls_when_it_runs_out(void) {
    setup();
    terminal_write(&terminal, "A\nB\nC\n");

    /* Three rows written and a fourth newline: everything moved up one. */
    CHECK(terminal.row == ROWS - 1U);
    CHECK(cell_is(0, 0, "...."
                        "####"));       /* B, was on row 1 */
    CHECK(cell_is(0, 1, "#..."
                        "#..."));       /* C, was on row 2 */

    /* And the line uncovered at the bottom is blank, not a copy of the old one. */
    CHECK(cell_is(0, ROWS - 1U, "...."
                                "...."));
}

static void scrolling_keeps_the_cursor_on_the_last_line(void) {
    setup();
    for (unsigned index = 0; index < 20; index++) {
        terminal_write(&terminal, "AB\n");
        CHECK(terminal.row < ROWS);
        CHECK(terminal.column < COLUMNS);
    }
    CHECK(terminal.row == ROWS - 1U);
}

static void clearing_resets_the_cursor_and_the_screen(void) {
    setup();
    terminal_write(&terminal, "AB\nCA");
    terminal_clear(&terminal);

    CHECK(terminal.column == 0 && terminal.row == 0);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++) CHECK(!lit(x, y));
}

static void nothing_is_drawn_outside_the_framebuffer(void) {
    setup();
    for (unsigned index = 0; index < 200; index++)
        terminal_put(&terminal, 'A');

    /* The padding after each row is what a bad pitch or a missing bound would
       spill into. */
    for (uint32_t y = 0; y < HEIGHT; y++) {
        const uint8_t *tail = pixels + (size_t)y * PITCH + WIDTH * 4U;
        for (size_t index = 0; index < PITCH - WIDTH * 4U; index++)
            CHECK(tail[index] == 0);
    }
}

TEST_MAIN(
    works_out_its_own_size();
    refuses_what_it_cannot_draw_on();
    draws_a_character_where_the_cursor_is();
    a_character_the_font_lacks_is_drawn_as_a_block();
    a_glyph_paints_its_own_background();
    the_colour_changes_between_characters();
    a_newline_moves_to_the_start_of_the_next_line();
    a_carriage_return_stays_on_the_line();
    a_tab_moves_to_the_next_stop();
    text_wraps_at_the_edge();
    the_screen_scrolls_when_it_runs_out();
    scrolling_keeps_the_cursor_on_the_last_line();
    clearing_resets_the_cursor_and_the_screen();
    nothing_is_drawn_outside_the_framebuffer();
)
