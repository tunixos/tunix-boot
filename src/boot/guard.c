#include <stdint.h>

#include "boot/guard.h"
#include "cpu/control.h"
#include "log/log.h"

/*
 * The stack canary.
 *
 * Everything this loader parses came off a disk or out of firmware, and most of
 * those parsers write into a buffer on the stack. The bounds checks are meant
 * to be enough; this is what happens when one of them is not.
 *
 * There is no entropy source before the kernel runs, so the value is the
 * timestamp counter stirred. That is not secret from anyone who can already run
 * code here — but nothing that can already run code here needs a stack overflow
 * — and it does defeat a fixed value baked into an exploit written against this
 * loader once and reused.
 */

#define GUARD_MIX_SHIFT 17U
#define GUARD_MIX_ODD 0x9E3779B97F4A7C15ULL

/* The low byte is cleared on purpose. A canary containing a zero byte cannot be
   reproduced by an overflow that copies a string, because the copy stops at the
   zero it would have to write. */
#define GUARD_LOW_BYTE_MASK 0xFFFFFFFFFFFFFF00ULL

static uint64_t make_guard(void) {
    uint64_t value = control_read_timestamp();
    value ^= value >> GUARD_MIX_SHIFT;
    value *= GUARD_MIX_ODD;
    value ^= value >> GUARD_MIX_SHIFT;
    return value & GUARD_LOW_BYTE_MASK;
}

static void guard_failed(void) {
    /* Nothing above this can be trusted, including whatever return address the
       stack now holds, so there is nowhere to go back to. */
    LOG_ERROR("stack canary overwritten; refusing to continue");
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* Each compiler this is built with emits its own names for these. */
#if defined(_WIN32) || defined(__MINGW32__)

uintptr_t __security_cookie;

void guard_init(void) {
    __security_cookie = (uintptr_t)make_guard();
}

void __security_check_cookie(uintptr_t found);
void __security_check_cookie(uintptr_t found) {
    if (found == __security_cookie) return;
    guard_failed();
}

#else

uintptr_t __stack_chk_guard;

void guard_init(void) {
    __stack_chk_guard = (uintptr_t)make_guard();
}

void __stack_chk_fail(void);
void __stack_chk_fail(void) {
    guard_failed();
}

#endif
