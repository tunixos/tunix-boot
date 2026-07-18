#include "harness.h"
#include "fw/uefi/graphics.h"

#define BASE 0x80000000ULL
#define WIDTH 1024U
#define HEIGHT 768U

static struct framebuffer fb;

static bool describe(uint32_t format, const struct uefi_pixel_masks *masks,
                     uint32_t scanline) {
    return uefi_graphics_describe(format, masks, WIDTH, HEIGHT, scanline, BASE,
                                  &fb);
}

/* What OVMF actually reports. */
static void describes_the_layout_ovmf_reports(void) {
    CHECK(describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH));

    CHECK(fb.base == (void *)(uintptr_t)BASE);
    CHECK(fb.width == WIDTH && fb.height == HEIGHT);
    CHECK(fb.bits_per_pixel == 32);
    CHECK(fb.pitch == WIDTH * 4U);

    /* Blue lowest, then green, then red — the order the name is written in
       backwards, which is exactly the thing to get wrong. */
    CHECK(fb.blue.shift == 0 && fb.blue.bits == 8);
    CHECK(fb.green.shift == 8);
    CHECK(fb.red.shift == 16);
    CHECK(framebuffer_pack(&fb, 0xFF, 0, 0) == 0x00FF0000);
}

static void describes_the_other_named_layout(void) {
    CHECK(describe(UEFI_PIXEL_RGB_RESERVED_8, NULL, WIDTH));

    CHECK(fb.red.shift == 0);
    CHECK(fb.green.shift == 8);
    CHECK(fb.blue.shift == 16);
    /* The same colour must come out the other way round, or the loader paints
       red where it meant blue on half the machines it runs on. */
    CHECK(framebuffer_pack(&fb, 0xFF, 0, 0) == 0x000000FF);
}

static void describes_a_masked_layout(void) {
    struct uefi_pixel_masks masks = {
        .red = 0x00FF0000,
        .green = 0x0000FF00,
        .blue = 0x000000FF,
        .reserved = 0xFF000000,
    };
    CHECK(describe(UEFI_PIXEL_BIT_MASK, &masks, WIDTH));

    CHECK(fb.red.shift == 16 && fb.red.bits == 8);
    CHECK(fb.green.shift == 8 && fb.green.bits == 8);
    CHECK(fb.blue.shift == 0 && fb.blue.bits == 8);
    CHECK(framebuffer_pack(&fb, 0x12, 0x34, 0x56) == 0x00123456);
}

static void a_padded_mode_gets_the_pitch_from_the_scanline_count(void) {
    /* Rows longer than the picture is wide. Taking the pitch from the width
       instead draws every row a little further left than the last. */
    CHECK(describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH + 64U));
    CHECK(fb.pitch == (WIDTH + 64U) * 4U);
    CHECK(fb.width == WIDTH);
    CHECK(framebuffer_valid(&fb));
}

static void a_scanline_shorter_than_the_width_is_refused(void) {
    /* Rows would overlap, and every write past the first would land in the row
       above; there is no sensible thing to do but refuse. */
    CHECK(!describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH - 1U));
}

static void a_blt_only_mode_is_refused(void) {
    /* No linear framebuffer exists in this mode — the firmware will copy
       rectangles and nothing else, so there is nothing to draw on. */
    CHECK(!describe(UEFI_PIXEL_BLT_ONLY, NULL, WIDTH));
    CHECK(!describe(9999, NULL, WIDTH));
}

static void a_masked_mode_without_masks_is_refused(void) {
    CHECK(!describe(UEFI_PIXEL_BIT_MASK, NULL, WIDTH));

    struct uefi_pixel_masks empty = {0, 0, 0, 0};
    CHECK(!describe(UEFI_PIXEL_BIT_MASK, &empty, WIDTH));
}

static void channels_that_share_bits_are_refused(void) {
    struct uefi_pixel_masks overlapping = {
        .red = 0x0000FF00,
        .green = 0x0000FF00,
        .blue = 0x000000FF,
    };
    /* Each would overwrite the other and the picture would come out in one
       colour, which looks like a broken screen rather than a broken loader. */
    CHECK(!describe(UEFI_PIXEL_BIT_MASK, &overlapping, WIDTH));
}

static void a_mask_with_a_hole_is_refused(void) {
    struct framebuffer_channel channel;
    CHECK(!uefi_channel_from_mask(0x00FF00FF, &channel));
    CHECK(!uefi_channel_from_mask(0, &channel));
    /* Wider than a channel can be. */
    CHECK(!uefi_channel_from_mask(0x0000FFFF, &channel));

    CHECK(uefi_channel_from_mask(0x000000FF, &channel));
    CHECK(channel.shift == 0 && channel.bits == 8);
    CHECK(uefi_channel_from_mask(0xFF000000, &channel));
    CHECK(channel.shift == 24 && channel.bits == 8);
    CHECK(uefi_channel_from_mask(0x000007E0, &channel));
    CHECK(channel.shift == 5 && channel.bits == 6);
}

static void a_mode_with_no_framebuffer_address_is_refused(void) {
    CHECK(!uefi_graphics_describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH, HEIGHT,
                                  WIDTH, 0, &fb));
    CHECK(!uefi_graphics_describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, 0, HEIGHT,
                                  WIDTH, BASE, &fb));
    CHECK(!uefi_graphics_describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH, 0,
                                  WIDTH, BASE, &fb));
    CHECK(!uefi_graphics_describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH, HEIGHT,
                                  WIDTH, BASE, NULL));
}

static void a_scanline_count_that_would_overflow_the_pitch_is_refused(void) {
    CHECK(!uefi_graphics_describe(UEFI_PIXEL_BGR_RESERVED_8, NULL, WIDTH, HEIGHT,
                                  UINT32_MAX, BASE, &fb));
}

TEST_MAIN(
    describes_the_layout_ovmf_reports();
    describes_the_other_named_layout();
    describes_a_masked_layout();
    a_padded_mode_gets_the_pitch_from_the_scanline_count();
    a_scanline_shorter_than_the_width_is_refused();
    a_blt_only_mode_is_refused();
    a_masked_mode_without_masks_is_refused();
    channels_that_share_bits_are_refused();
    a_mask_with_a_hole_is_refused();
    a_mode_with_no_framebuffer_address_is_refused();
    a_scanline_count_that_would_overflow_the_pitch_is_refused();
)
