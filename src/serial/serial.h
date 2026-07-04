#ifndef TUNIX_BOOT_SERIAL_SERIAL_H
#define TUNIX_BOOT_SERIAL_SERIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A 16550 line, which is what every emulator and most machines still expose,
   and the only output that exists before a framebuffer does. */
bool serial_init(uint16_t port);
void serial_write(const char *text, size_t length);
void serial_write_cstr(const char *text);

#endif
