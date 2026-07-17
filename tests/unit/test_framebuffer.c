#include "harness.h"
#include "video/framebuffer.h"
#include "util/mem.h"

#define WIDTH 64U
#define HEIGHT 32U
/* Deliberately more than the rows need, because firmware pads them and code
   that assumes otherwise draws a sheared picture. */
#define PADDING_BYTES 24U
#define PITCH (WIDTH * 4U + PADDING_BYTES)

static uint8_t pixels[PITCH * HEIGHT];
static struct framebuffer fb;

/* The two layouts UEFI names, and one 16-bit mode where the channels are not
   byte aligned at all. */
static void setup_bgrx(void) {
    memset(pixels, 0, sizeof pixels);
    fb.base = pixels;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.pitch = PITCH;
    fb.bits_per_pixel = 32;
    fb.red = (struct framebuffer_channel){16, 8};
    fb.green = (struct framebuffer_channel){8, 8};
    fb.blue = (struct framebuffer_channel){0, 8};
}

static void setup_rgbx(void) {
    setup_bgrx();
    fb.red = (struct framebuffer_channel){0, 8};
    fb.blue = (struct framebuffer_channel){16, 8};
}

static void setup_565(void) {
    setup_bgrx();
    fb.bits_per_pixel = 16;
    fb.pitch = WIDTH * 2U + PADDING_BYTES;
    fb.red = (struct framebuffer_channel){11, 5};
    fb.green = (struct framebuffer_channel){5, 6};
    fb.blue = (struct framebuffer_channel){0, 5};
}

static void accepts_what_it_can_draw_on(void) {
    setup_bgrx();
    CHECK(framebuffer_valid(&fb));
    CHECK(!framebuffer_valid(NULL));

    struct framebuffer broken = fb;
    broken.base = NULL;
    CHECK(!framebuffer_valid(&broken));

    broken = fb;
    broken.width = 0;
    CHECK(!framebuffer_valid(&broken));

    /* A pitch shorter than a row makes rows overlap, and every write past the
       first lands in the row above. */
    broken = fb;
    broken.pitch = WIDTH * 4U - 1U;
    CHECK(!framebuffer_valid(&broken));

    /* A channel running off the end of a pixel writes its top bits into
       whatever comes next. */
    broken = fb;
    broken.red = (struct framebuffer_channel){28, 8};
    CHECK(!framebuffer_valid(&broken));

    broken = fb;
    broken.green = (struct framebuffer_channel){0, 0};
    CHECK(!framebuffer_valid(&broken));

    broken = fb;
    broken.bits_per_pixel = 8;
    CHECK(!framebuffer_valid(&broken));

    setup_565();
    CHECK(framebuffer_valid(&fb));
}

static void packs_a_colour_the_way_the_firmware_wants_it(void) {
    setup_bgrx();
    CHECK(framebuffer_pack(&fb, 0xFF, 0x00, 0x00) == 0x00FF0000);
    CHECK(framebuffer_pack(&fb, 0x00, 0xFF, 0x00) == 0x0000FF00);
    CHECK(framebuffer_pack(&fb, 0x00, 0x00, 0xFF) == 0x000000FF);
    CHECK(framebuffer_pack(&fb, 0x12, 0x34, 0x56) == 0x00123456);

    /* The same colour in the other layout must come out swapped, or the loader
       paints in the wrong colours on half the machines it runs on. */
    setup_rgbx();
    CHECK(framebuffer_pack(&fb, 0xFF, 0x00, 0x00) == 0x000000FF);
    CHECK(framebuffer_pack(&fb, 0x12, 0x34, 0x56) == 0x00563412);
}

static void narrow_channels_keep_their_high_bits(void) {
    setup_565();
    /* Masking off the high bits instead would make white come out black. */
    CHECK(framebuffer_pack(&fb, 0xFF, 0xFF, 0xFF) == 0xFFFF);
    CHECK(framebuffer_pack(&fb, 0xFF, 0x00, 0x00) == 0xF800);
    CHECK(framebuffer_pack(&fb, 0x00, 0xFF, 0x00) == 0x07E0);
    CHECK(framebuffer_pack(&fb, 0x00, 0x00, 0xFF) == 0x001F);
    /* And a mid grey stays a mid grey rather than becoming dim. */
    CHECK(framebuffer_pack(&fb, 0x80, 0x80, 0x80) == 0x8410);
}

static void writes_a_pixel_where_it_says(void) {
    setup_bgrx();
    framebuffer_put(&fb, 3, 2, 0x00AABBCC);
    CHECK(framebuffer_get(&fb, 3, 2) == 0x00AABBCC);

    /* At the right offset for the pitch, not for a row of width * 4. */
    const uint8_t *at = pixels + 2U * PITCH + 3U * 4U;
    CHECK(at[0] == 0xCC && at[1] == 0xBB && at[2] == 0xAA);

    CHECK(framebuffer_get(&fb, 4, 2) == 0);
    CHECK(framebuffer_get(&fb, 3, 3) == 0);
}

static void a_sixteen_bit_pixel_is_two_bytes(void) {
    setup_565();
    framebuffer_put(&fb, 5, 1, 0xF81F);
    CHECK(framebuffer_get(&fb, 5, 1) == 0xF81F);

    const uint8_t *at = pixels + 1U * fb.pitch + 5U * 2U;
    CHECK(at[0] == 0x1F && at[1] == 0xF8);
    /* The byte after must be untouched, or every pixel overwrites its neighbour. */
    CHECK(at[2] == 0);
}

static void a_pixel_outside_the_screen_is_dropped(void) {
    setup_bgrx();
    framebuffer_put(&fb, WIDTH, 0, 0xFFFFFF);
    framebuffer_put(&fb, 0, HEIGHT, 0xFFFFFF);
    framebuffer_put(&fb, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF);

    /* Nothing was written anywhere: a terminal at the bottom of the screen
       relies on this rather than checking every glyph twice. */
    for (size_t index = 0; index < sizeof pixels; index++)
        CHECK(pixels[index] == 0);
    CHECK(framebuffer_get(&fb, WIDTH, 0) == 0);
}

static void fills_a_rectangle(void) {
    setup_bgrx();
    framebuffer_fill(&fb, 2, 3, 4, 5, 0x00112233);

    CHECK(framebuffer_get(&fb, 2, 3) == 0x00112233);
    CHECK(framebuffer_get(&fb, 5, 7) == 0x00112233);
    /* Just outside on each side. */
    CHECK(framebuffer_get(&fb, 1, 3) == 0);
    CHECK(framebuffer_get(&fb, 6, 3) == 0);
    CHECK(framebuffer_get(&fb, 2, 2) == 0);
    CHECK(framebuffer_get(&fb, 2, 8) == 0);
}

static void a_rectangle_is_clipped_not_refused(void) {
    setup_bgrx();
    framebuffer_fill(&fb, WIDTH - 2U, HEIGHT - 2U, 100, 100, 0x00445566);

    CHECK(framebuffer_get(&fb, WIDTH - 1U, HEIGHT - 1U) == 0x00445566);
    CHECK(framebuffer_get(&fb, WIDTH - 2U, HEIGHT - 2U) == 0x00445566);

    /* The padding after the last row must not have been written through. */
    const uint8_t *tail = pixels + (size_t)(HEIGHT - 1U) * PITCH + WIDTH * 4U;
    for (size_t index = 0; index < PADDING_BYTES; index++)
        CHECK(tail[index] == 0);

    /* Wholly outside does nothing at all. */
    framebuffer_fill(&fb, WIDTH, 0, 8, 8, 0xFFFFFF);
    framebuffer_fill(&fb, 0, HEIGHT, 8, 8, 0xFFFFFF);
}

static void scrolls_rows_upwards(void) {
    setup_bgrx();
    for (uint32_t y = 0; y < HEIGHT; y++)
        framebuffer_fill(&fb, 0, y, WIDTH, 1, 0x00010000 * y);

    /* What a terminal does when the cursor runs off the bottom. */
    framebuffer_move_rows(&fb, 0, 4, HEIGHT - 4U);

    for (uint32_t y = 0; y < HEIGHT - 4U; y++)
        CHECK(framebuffer_get(&fb, 7, y) == 0x00010000 * (y + 4U));
    /* The rows the move read from are still there; clearing them is the
       caller's business. */
    CHECK(framebuffer_get(&fb, 7, HEIGHT - 1U) == 0x00010000 * (HEIGHT - 1U));
}

static void scrolls_rows_downwards_without_smearing(void) {
    setup_bgrx();
    for (uint32_t y = 0; y < HEIGHT; y++)
        framebuffer_fill(&fb, 0, y, WIDTH, 1, 0x00000100 * y);

    /* Overlapping and moving forward: copying front to back here would smear
       the first row over everything below it. */
    framebuffer_move_rows(&fb, 4, 0, HEIGHT - 4U);

    for (uint32_t y = 4; y < HEIGHT; y++)
        CHECK(framebuffer_get(&fb, 7, y) == 0x00000100 * (y - 4U));
}

static void a_move_is_clipped_to_the_screen(void) {
    setup_bgrx();
    framebuffer_fill(&fb, 0, 0, WIDTH, HEIGHT, 0x00778899);

    framebuffer_move_rows(&fb, 0, HEIGHT - 2U, 100);
    framebuffer_move_rows(&fb, HEIGHT, 0, 4);
    framebuffer_move_rows(&fb, 0, HEIGHT, 4);
    framebuffer_move_rows(&fb, 3, 3, 4);

    /* Whatever it did, it stayed inside: the padding is still untouched. */
    const uint8_t *tail = pixels + (size_t)(HEIGHT - 1U) * PITCH + WIDTH * 4U;
    for (size_t index = 0; index < PADDING_BYTES; index++)
        CHECK(tail[index] == 0);
}

TEST_MAIN(
    accepts_what_it_can_draw_on();
    packs_a_colour_the_way_the_firmware_wants_it();
    narrow_channels_keep_their_high_bits();
    writes_a_pixel_where_it_says();
    a_sixteen_bit_pixel_is_two_bytes();
    a_pixel_outside_the_screen_is_dropped();
    fills_a_rectangle();
    a_rectangle_is_clipped_not_refused();
    scrolls_rows_upwards();
    scrolls_rows_downwards_without_smearing();
    a_move_is_clipped_to_the_screen();
)
