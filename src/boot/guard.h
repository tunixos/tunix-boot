#ifndef TUNIX_BOOT_BOOT_GUARD_H
#define TUNIX_BOOT_BOOT_GUARD_H

/* Seeds the stack canary. Must run before anything that has one returns, which
   means first, before the entry point calls anything else. */
void guard_init(void);

#endif
