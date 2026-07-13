#ifndef TUNIX_BOOT_CONFIG_CONFIG_H
#define TUNIX_BOOT_CONFIG_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/str.h"

/*
 * The file that says what to boot.
 *
 * Every value points into the text it was parsed from and nothing is copied, so
 * the buffer holding the file must outlive the config. That keeps the parser
 * allocation-free, which matters because it runs before there is much to
 * allocate from and because a parser that cannot fail for want of memory has
 * one fewer way to fail.
 *
 * A file that does not parse is refused whole, with the line that broke it. A
 * bootloader that skips the line it did not understand boots something other
 * than what it was told to, and does it silently.
 *
 *     # comment
 *     timeout = 5
 *     default = tunix
 *
 *     :tunix
 *     kernel = /kernel.elf
 *     cmdline = root=/dev/sda1 quiet
 *     module = /initramfs.img
 */

#define CONFIG_MAX_ENTRIES 16U
#define CONFIG_MAX_MODULES 8U
#define CONFIG_MAX_BYTES 65536U
#define CONFIG_MAX_LINE_BYTES 1024U

#define CONFIG_COMMENT_CHARACTER '#'
#define CONFIG_ENTRY_CHARACTER ':'
#define CONFIG_ASSIGNMENT_CHARACTER '='

#define CONFIG_DEFAULT_TIMEOUT_SECONDS 5U
#define CONFIG_MAX_TIMEOUT_SECONDS 3600U

struct config_entry {
    struct str name;
    struct str kernel;
    struct str cmdline;
    struct str modules[CONFIG_MAX_MODULES];
    unsigned module_count;
};

struct config {
    uint64_t timeout_seconds;
    struct str default_entry;
    struct config_entry entries[CONFIG_MAX_ENTRIES];
    unsigned entry_count;
};

/* Which line stopped the parse, counted from one, and what was wrong with it.
   The message is a literal, never the file's own text. */
struct config_error {
    size_t line;
    const char *reason;
};

bool config_parse(const char *text, size_t length, struct config *out,
                  struct config_error *error);

/* The entry `default` names, or the first one if it names nothing. NULL when
   there are no entries or the name matches none of them — the second is worth
   distinguishing from the first, so the caller is told which. */
const struct config_entry *config_default_entry(const struct config *config);

const struct config_entry *config_find(const struct config *config,
                                       struct str name);

#endif
