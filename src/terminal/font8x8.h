#ifndef TUNIX_BOOT_TERMINAL_FONT8X8_H
#define TUNIX_BOOT_TERMINAL_FONT8X8_H

#include "terminal/terminal.h"

/* The printable range, ' ' through '~'. */
#define FONT8X8_WIDTH 8U
#define FONT8X8_HEIGHT 8U
#define FONT8X8_FIRST 0x20U
#define FONT8X8_LAST 0x7EU
#define FONT8X8_COUNT (FONT8X8_LAST - FONT8X8_FIRST + 1U)

const struct terminal_font *font8x8(void);

#endif
