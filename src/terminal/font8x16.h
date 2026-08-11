#ifndef TUNIX_BOOT_TERMINAL_FONT8X16_H
#define TUNIX_BOOT_TERMINAL_FONT8X16_H

#include "terminal/terminal.h"

/* The printable range, ' ' through '~'. */
#define FONT8X16_WIDTH 8U
#define FONT8X16_HEIGHT 16U
#define FONT8X16_FIRST 0x20U
#define FONT8X16_LAST 0x7EU
#define FONT8X16_COUNT (FONT8X16_LAST - FONT8X16_FIRST + 1U)

const struct terminal_font *font8x16(void);

#endif
