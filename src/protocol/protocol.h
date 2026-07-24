#ifndef TUNIX_BOOT_PROTOCOL_PROTOCOL_H
#define TUNIX_BOOT_PROTOCOL_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/arena.h"
#include "memory/map.h"
#include "video/framebuffer.h"
#include "acpi/acpi.h"

/*
 * What the loader tells the kernel, and how the kernel asks.
 *
 * The kernel puts request structures in its own image and the loader finds them
 * by scanning for their magic. That inverts the usual arrangement, where the
 * loader hands over a fixed structure and every new field is an ABI break: here
 * a kernel asks only for what it understands, an older kernel ignores anything
 * added since, and a newer kernel finds its response pointer still null when the
 * loader has never heard of the request.
 *
 * Every response is allocated in memory the kernel is free to reclaim once it
 * has copied what it wants, and is reported as such in the memory map.
 *
 * This header is the ABI. A kernel includes it; nothing in it may change
 * meaning without the revision changing with it.
 */

/* Chosen to be improbable in code or data, and checked as a pair so that one
   half appearing by accident is not enough. */
#define BOOT_REQUEST_MAGIC_LOW 0x5449554e49582d31ULL
#define BOOT_REQUEST_MAGIC_HIGH 0x424f4f54503a3031ULL

#define BOOT_PROTOCOL_REVISION 0U

#define BOOT_REQUEST_MEMORY_MAP 1U
#define BOOT_REQUEST_KERNEL_ADDRESS 2U
#define BOOT_REQUEST_COMMAND_LINE 3U
#define BOOT_REQUEST_LOADER_INFO 4U
#define BOOT_REQUEST_FRAMEBUFFER 5U
#define BOOT_REQUEST_ACPI 6U
#define BOOT_REQUEST_PROCESSORS 7U
#define BOOT_REQUEST_MODULES 8U

/* The scan steps by this, so a request must be aligned to it. Everything a
   compiler emits for a structure containing a uint64_t already is. */
#define BOOT_REQUEST_ALIGNMENT 8U

/* A crafted image could otherwise have the loader answer the same request over
   and over until the arena is gone. */
#define BOOT_MAX_REQUESTS 64U

struct boot_request {
    uint64_t magic[2];
    uint64_t id;
    /* What the kernel understands. The loader answers at the lower of this and
       its own, and says which in the response. */
    uint64_t revision;
    /* Null until the loader fills it in. A kernel finding it still null asked
       for something this loader does not provide, which is not an error — it is
       the kernel's business what to do about it. */
    void *response;
};

/* Mirrors enum memory_kind. Repeated rather than shared so that reordering the
   loader's own enum cannot silently change what a kernel is told. */
#define BOOT_MEMORY_USABLE 0U
#define BOOT_MEMORY_RECLAIMABLE 1U
#define BOOT_MEMORY_ACPI_RECLAIMABLE 2U
#define BOOT_MEMORY_ACPI_NVS 3U
#define BOOT_MEMORY_FRAMEBUFFER 4U
#define BOOT_MEMORY_KERNEL 5U
#define BOOT_MEMORY_RESERVED 6U
#define BOOT_MEMORY_BAD 7U

struct boot_memory_entry {
    uint64_t base;
    uint64_t length;
    uint64_t kind;
};

struct boot_memory_map_response {
    uint64_t revision;
    uint64_t entry_count;
    struct boot_memory_entry *entries;
};

struct boot_kernel_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct boot_command_line_response {
    uint64_t revision;
    /* NUL terminated, and never null: a kernel given no command line gets an
       empty string, so it has one thing to handle rather than two. */
    const char *command_line;
};

/* Channels as a shift and a width, the same way the loader carries them, so a
   kernel can pack a colour without knowing which firmware described the mode. */
struct boot_framebuffer_response {
    uint64_t revision;
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint8_t red_shift;
    uint8_t red_bits;
    uint8_t green_shift;
    uint8_t green_bits;
    uint8_t blue_shift;
    uint8_t blue_bits;
    uint8_t reserved[2];
};

/* The address of the RSDP, not a copy of it: the tables it leads to stay where
   the firmware put them, and the kernel walks them itself. */
struct boot_acpi_response {
    uint64_t revision;
    uint64_t rsdp;
};

struct boot_processor {
    uint32_t apic_id;
    uint32_t startable;
};

struct boot_processors_response {
    uint64_t revision;
    uint64_t count;
    uint64_t local_apic_address;
    struct boot_processor *entries;
};

/* A file the configuration named, loaded whole and left where it was put. The
   memory is reported as bootloader-reclaimable like everything else the loader
   allocated, so a kernel that has consumed a module may take the space back. */
struct boot_module {
    uint64_t base;
    uint64_t bytes;
    const char *path;
};

struct boot_modules_response {
    uint64_t revision;
    uint64_t count;
    struct boot_module *entries;
};

struct boot_loader_info_response {
    uint64_t revision;
    const char *name;
    const char *version;
};

/* Everything the loader has to answer with. */
struct boot_facts {
    const struct memory_map *memory;
    uint64_t kernel_physical_base;
    uint64_t kernel_virtual_base;
    const char *command_line;
    /* NULL when the machine has no display the loader could find, which is not
       an error — the kernel's request simply goes unanswered. */
    const struct framebuffer *screen;
    /* NULL when the machine has no ACPI tables the loader could believe. */
    const void *rsdp;
    const struct acpi_processors *processors;
    const struct boot_module *modules;
    unsigned module_count;
};

/* Scans `bytes` from `image` for requests and answers each one, allocating
   responses from `arena`. Returns how many were answered, or -1 if the arena ran
   out — a partly answered kernel is one that cannot be trusted to start. */
int protocol_answer(void *image, uint64_t bytes, const struct boot_facts *facts,
                    struct arena *arena);

uint64_t protocol_memory_kind(enum memory_kind kind);

#endif
