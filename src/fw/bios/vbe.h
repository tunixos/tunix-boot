#ifndef TUNIX_BOOT_FW_BIOS_VBE_H
#define TUNIX_BOOT_FW_BIOS_VBE_H

#include <stdbool.h>
#include <stdint.h>

#include "video/framebuffer.h"

/*
 * The mode information block VBE returns, turned into a framebuffer.
 *
 * INT 10h is a real-mode service, so the same arrangement as E820: stage2 finds
 * a mode and sets it while it still can, leaves the block it was given at a
 * fixed address, and this reads it. The assembly decides nothing it does not
 * have to; every field that could be wrong is checked here, where it can be
 * tested against blocks no card would produce.
 */

#define VBE_ATTRIBUTES_OFFSET 0x00U
#define VBE_PITCH_OFFSET 0x10U
#define VBE_WIDTH_OFFSET 0x12U
#define VBE_HEIGHT_OFFSET 0x14U
#define VBE_BITS_PER_PIXEL_OFFSET 0x19U
#define VBE_MEMORY_MODEL_OFFSET 0x1BU
#define VBE_RED_BITS_OFFSET 0x1FU
#define VBE_RED_SHIFT_OFFSET 0x20U
#define VBE_GREEN_BITS_OFFSET 0x21U
#define VBE_GREEN_SHIFT_OFFSET 0x22U
#define VBE_BLUE_BITS_OFFSET 0x23U
#define VBE_BLUE_SHIFT_OFFSET 0x24U
#define VBE_FRAMEBUFFER_OFFSET 0x28U
#define VBE_MODE_INFO_BYTES 256U

#define VBE_ATTRIBUTE_SUPPORTED (1U << 0)
#define VBE_ATTRIBUTE_GRAPHICS (1U << 4)
/* Without this the mode has no linear framebuffer and is addressed through a
   64 KiB window, which is not something this loader will do. */
#define VBE_ATTRIBUTE_LINEAR (1U << 7)

#define VBE_MEMORY_MODEL_DIRECT_COLOUR 6U

/* Turns the block at `mode_info` into a framebuffer. False for a mode this
   loader cannot draw on, whatever the card said about it. */
bool vbe_describe(const void *mode_info, struct framebuffer *out);

#endif
