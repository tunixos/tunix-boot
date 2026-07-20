#include "harness.h"
#include "fw/bios/vbe.h"
#include "util/mem.h"

static uint8_t block[VBE_MODE_INFO_BYTES];
static struct framebuffer fb;

static void put_u16(unsigned offset, uint16_t value) {
    block[offset] = (uint8_t)(value & 0xFFU);
    block[offset + 1U] = (uint8_t)(value >> 8);
}

static void put_u32(unsigned offset, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        block[offset + index] = (uint8_t)(value >> (index * 8));
}

/* A 1024x768x32 mode as a card really reports one. */
static void good_mode(void) {
    memset(block, 0, sizeof block);
    put_u16(VBE_ATTRIBUTES_OFFSET, VBE_ATTRIBUTE_SUPPORTED |
                                       VBE_ATTRIBUTE_GRAPHICS |
                                       VBE_ATTRIBUTE_LINEAR);
    put_u16(VBE_PITCH_OFFSET, 4096);
    put_u16(VBE_WIDTH_OFFSET, 1024);
    put_u16(VBE_HEIGHT_OFFSET, 768);
    block[VBE_BITS_PER_PIXEL_OFFSET] = 32;
    block[VBE_MEMORY_MODEL_OFFSET] = VBE_MEMORY_MODEL_DIRECT_COLOUR;
    block[VBE_RED_BITS_OFFSET] = 8;
    block[VBE_RED_SHIFT_OFFSET] = 16;
    block[VBE_GREEN_BITS_OFFSET] = 8;
    block[VBE_GREEN_SHIFT_OFFSET] = 8;
    block[VBE_BLUE_BITS_OFFSET] = 8;
    block[VBE_BLUE_SHIFT_OFFSET] = 0;
    put_u32(VBE_FRAMEBUFFER_OFFSET, 0xFD000000);
}

static void describes_a_mode_a_card_reports(void) {
    good_mode();
    CHECK(vbe_describe(block, &fb));

    CHECK(fb.base == (void *)(uintptr_t)0xFD000000);
    CHECK(fb.width == 1024 && fb.height == 768);
    CHECK(fb.pitch == 4096);
    CHECK(fb.bits_per_pixel == 32);
    CHECK(framebuffer_pack(&fb, 0xFF, 0x00, 0x00) == 0x00FF0000);
    CHECK(framebuffer_pack(&fb, 0x00, 0x00, 0xFF) == 0x000000FF);
}

/* VBE gives the size and the position separately, and in that order — the one
   thing to get wrong here is reading them the other way round. */
static void reads_the_size_before_the_position(void) {
    good_mode();
    block[VBE_RED_BITS_OFFSET] = 8;
    block[VBE_RED_SHIFT_OFFSET] = 16;
    CHECK(vbe_describe(block, &fb));
    CHECK(fb.red.bits == 8 && fb.red.shift == 16);
}

static void describes_a_sixteen_bit_mode(void) {
    good_mode();
    put_u16(VBE_PITCH_OFFSET, 2048);
    block[VBE_BITS_PER_PIXEL_OFFSET] = 16;
    block[VBE_RED_BITS_OFFSET] = 5;
    block[VBE_RED_SHIFT_OFFSET] = 11;
    block[VBE_GREEN_BITS_OFFSET] = 6;
    block[VBE_GREEN_SHIFT_OFFSET] = 5;
    block[VBE_BLUE_BITS_OFFSET] = 5;
    block[VBE_BLUE_SHIFT_OFFSET] = 0;

    CHECK(vbe_describe(block, &fb));
    CHECK(fb.bits_per_pixel == 16);
    /* White must still be white, which is what keeping the high bits buys. */
    CHECK(framebuffer_pack(&fb, 0xFF, 0xFF, 0xFF) == 0xFFFF);
}

static void a_padded_mode_keeps_the_pitch_it_was_given(void) {
    good_mode();
    put_u16(VBE_PITCH_OFFSET, 4096 + 256U);
    CHECK(vbe_describe(block, &fb));
    CHECK(fb.pitch == 4096 + 256U);
    CHECK(fb.width == 1024);
}

static void a_mode_without_a_linear_framebuffer_is_refused(void) {
    good_mode();
    /* Addressed through a 64 KiB window instead, which this loader will not do. */
    put_u16(VBE_ATTRIBUTES_OFFSET,
            VBE_ATTRIBUTE_SUPPORTED | VBE_ATTRIBUTE_GRAPHICS);
    CHECK(!vbe_describe(block, &fb));
}

static void a_text_mode_is_refused(void) {
    good_mode();
    put_u16(VBE_ATTRIBUTES_OFFSET,
            VBE_ATTRIBUTE_SUPPORTED | VBE_ATTRIBUTE_LINEAR);
    CHECK(!vbe_describe(block, &fb));
}

static void an_unsupported_mode_is_refused(void) {
    good_mode();
    put_u16(VBE_ATTRIBUTES_OFFSET,
            VBE_ATTRIBUTE_GRAPHICS | VBE_ATTRIBUTE_LINEAR);
    CHECK(!vbe_describe(block, &fb));
}

static void a_paletted_mode_is_refused(void) {
    good_mode();
    /* A palette is a different thing to draw through than channel masks. */
    block[VBE_MEMORY_MODEL_OFFSET] = 4;
    CHECK(!vbe_describe(block, &fb));
}

static void a_mode_with_no_framebuffer_address_is_refused(void) {
    good_mode();
    put_u32(VBE_FRAMEBUFFER_OFFSET, 0);
    CHECK(!vbe_describe(block, &fb));
}

static void a_pitch_shorter_than_a_row_is_refused(void) {
    good_mode();
    /* Rows would overlap and every write past the first would land in the row
       above. The card should never say this; it is checked anyway. */
    put_u16(VBE_PITCH_OFFSET, 1024);
    CHECK(!vbe_describe(block, &fb));
}

static void a_nonsense_channel_is_refused(void) {
    good_mode();
    block[VBE_RED_SHIFT_OFFSET] = 30;
    CHECK(!vbe_describe(block, &fb));

    good_mode();
    block[VBE_GREEN_BITS_OFFSET] = 0;
    CHECK(!vbe_describe(block, &fb));

    good_mode();
    block[VBE_BLUE_BITS_OFFSET] = 9;
    CHECK(!vbe_describe(block, &fb));
}

static void a_zero_sized_mode_is_refused(void) {
    good_mode();
    put_u16(VBE_WIDTH_OFFSET, 0);
    CHECK(!vbe_describe(block, &fb));

    good_mode();
    put_u16(VBE_HEIGHT_OFFSET, 0);
    CHECK(!vbe_describe(block, &fb));
}

static void nothing_at_all_is_refused(void) {
    CHECK(!vbe_describe(NULL, &fb));
    good_mode();
    CHECK(!vbe_describe(block, NULL));

    memset(block, 0, sizeof block);
    CHECK(!vbe_describe(block, &fb));
}

TEST_MAIN(
    describes_a_mode_a_card_reports();
    reads_the_size_before_the_position();
    describes_a_sixteen_bit_mode();
    a_padded_mode_keeps_the_pitch_it_was_given();
    a_mode_without_a_linear_framebuffer_is_refused();
    a_text_mode_is_refused();
    an_unsupported_mode_is_refused();
    a_paletted_mode_is_refused();
    a_mode_with_no_framebuffer_address_is_refused();
    a_pitch_shorter_than_a_row_is_refused();
    a_nonsense_channel_is_refused();
    a_zero_sized_mode_is_refused();
    nothing_at_all_is_refused();
)
