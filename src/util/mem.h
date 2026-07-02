#ifndef TUNIX_BOOT_UTIL_MEM_H
#define TUNIX_BOOT_UTIL_MEM_H

#include <stddef.h>
#include <stdint.h>

/* Freestanding replacements for the few string.h routines the loader needs.
   The compiler may still emit calls to these from struct assignment, so they
   keep the standard names and signatures. */
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

#endif
