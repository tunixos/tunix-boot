#include "protocol/protocol.h"
#include "util/checked.h"
#include "util/mem.h"

#define LOADER_NAME "tunix-boot"
#define LOADER_VERSION "0.1.0"
#define EMPTY_COMMAND_LINE ""

uint64_t protocol_memory_kind(enum memory_kind kind) {
    switch (kind) {
        case MEMORY_KIND_USABLE:                 return BOOT_MEMORY_USABLE;
        case MEMORY_KIND_BOOTLOADER_RECLAIMABLE: return BOOT_MEMORY_RECLAIMABLE;
        case MEMORY_KIND_ACPI_RECLAIMABLE:       return BOOT_MEMORY_ACPI_RECLAIMABLE;
        case MEMORY_KIND_ACPI_NVS:               return BOOT_MEMORY_ACPI_NVS;
        case MEMORY_KIND_FRAMEBUFFER:            return BOOT_MEMORY_FRAMEBUFFER;
        case MEMORY_KIND_KERNEL:                 return BOOT_MEMORY_KERNEL;
        case MEMORY_KIND_RESERVED:               return BOOT_MEMORY_RESERVED;
        case MEMORY_KIND_BAD:                    return BOOT_MEMORY_BAD;
    }
    /* A kind with no name here is one the kernel has never been told about, and
       memory it does not understand is memory it must not use. */
    return BOOT_MEMORY_RESERVED;
}

/* The revision to answer at: what the kernel asked for, capped at what this
   loader knows. A kernel asking for more gets what there is and is told so. */
static uint64_t agreed_revision(uint64_t requested) {
    /* Written as "cap it" rather than "take the lower": with the revision at
       zero the other order compares an unsigned value against nothing. */
    return requested > BOOT_PROTOCOL_REVISION ? BOOT_PROTOCOL_REVISION : requested;
}

static bool answer_memory_map(struct boot_request *request,
                              const struct boot_facts *facts,
                              struct arena *arena) {
    struct boot_memory_map_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    size_t count = facts->memory ? facts->memory->count : 0;
    if (count > 0) {
        response->entries =
            arena_allocate_array(arena, count, sizeof *response->entries);
        if (!response->entries) return false;

        for (size_t index = 0; index < count; index++) {
            const struct memory_region *region = &facts->memory->regions[index];
            response->entries[index].base = region->base;
            response->entries[index].length = region->length;
            response->entries[index].kind = protocol_memory_kind(region->kind);
        }
    }

    response->revision = agreed_revision(request->revision);
    response->entry_count = count;
    request->response = response;
    return true;
}

static bool answer_kernel_address(struct boot_request *request,
                                  const struct boot_facts *facts,
                                  struct arena *arena) {
    struct boot_kernel_address_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    response->revision = agreed_revision(request->revision);
    response->physical_base = facts->kernel_physical_base;
    response->virtual_base = facts->kernel_virtual_base;
    request->response = response;
    return true;
}

static bool answer_command_line(struct boot_request *request,
                                const struct boot_facts *facts,
                                struct arena *arena) {
    struct boot_command_line_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    response->revision = agreed_revision(request->revision);
    response->command_line =
        facts->command_line ? facts->command_line : EMPTY_COMMAND_LINE;
    request->response = response;
    return true;
}

static bool answer_framebuffer(struct boot_request *request,
                               const struct boot_facts *facts,
                               struct arena *arena) {
    /* A machine with no screen leaves the response null, which is the protocol
       already saying "there is none" — inventing an empty one would have the
       kernel draw into address zero. */
    if (!facts->screen) return true;

    struct boot_framebuffer_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    const struct framebuffer *screen = facts->screen;
    response->revision = agreed_revision(request->revision);
    response->base = (uint64_t)(uintptr_t)screen->base;
    response->width = screen->width;
    response->height = screen->height;
    response->pitch = screen->pitch;
    response->bits_per_pixel = screen->bits_per_pixel;
    response->red_shift = screen->red.shift;
    response->red_bits = screen->red.bits;
    response->green_shift = screen->green.shift;
    response->green_bits = screen->green.bits;
    response->blue_shift = screen->blue.shift;
    response->blue_bits = screen->blue.bits;

    request->response = response;
    return true;
}

static bool answer_acpi(struct boot_request *request,
                        const struct boot_facts *facts, struct arena *arena) {
    if (!facts->rsdp) return true;

    struct boot_acpi_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    response->revision = agreed_revision(request->revision);
    response->rsdp = (uint64_t)(uintptr_t)facts->rsdp;
    request->response = response;
    return true;
}

static bool answer_processors(struct boot_request *request,
                              const struct boot_facts *facts,
                              struct arena *arena) {
    if (!facts->processors || facts->processors->count == 0) return true;

    struct boot_processors_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    unsigned count = facts->processors->count;
    response->entries = arena_allocate_array(arena, count,
                                             sizeof *response->entries);
    if (!response->entries) return false;

    for (unsigned index = 0; index < count; index++) {
        response->entries[index].apic_id =
            facts->processors->entries[index].apic_id;
        response->entries[index].startable =
            facts->processors->entries[index].enabled ? 1U : 0U;
    }

    response->revision = agreed_revision(request->revision);
    response->count = count;
    response->local_apic_address = facts->processors->local_apic_address;
    request->response = response;
    return true;
}

static bool answer_loader_info(struct boot_request *request,
                               struct arena *arena) {
    struct boot_loader_info_response *response =
        arena_allocate_zeroed(arena, sizeof *response);
    if (!response) return false;

    response->revision = agreed_revision(request->revision);
    response->name = LOADER_NAME;
    response->version = LOADER_VERSION;
    request->response = response;
    return true;
}

static bool answer(struct boot_request *request, const struct boot_facts *facts,
                   struct arena *arena) {
    switch (request->id) {
        case BOOT_REQUEST_MEMORY_MAP:
            return answer_memory_map(request, facts, arena);
        case BOOT_REQUEST_KERNEL_ADDRESS:
            return answer_kernel_address(request, facts, arena);
        case BOOT_REQUEST_COMMAND_LINE:
            return answer_command_line(request, facts, arena);
        case BOOT_REQUEST_LOADER_INFO:
            return answer_loader_info(request, arena);
        case BOOT_REQUEST_FRAMEBUFFER:
            return answer_framebuffer(request, facts, arena);
        case BOOT_REQUEST_ACPI:
            return answer_acpi(request, facts, arena);
        case BOOT_REQUEST_PROCESSORS:
            return answer_processors(request, facts, arena);
        default:
            /* A request this loader has never heard of keeps its null response,
               which is the protocol's way of saying so. */
            return true;
    }
}

int protocol_answer(void *image, uint64_t bytes, const struct boot_facts *facts,
                    struct arena *arena) {
    if (!image || !facts || !arena) return -1;
    if (bytes < sizeof(struct boot_request)) return 0;

    uint8_t *base = (uint8_t *)image;
    /* The last position a whole request could start at, so the scan never reads
       a field that is only partly inside the image. */
    uint64_t last = bytes - sizeof(struct boot_request);
    unsigned answered = 0;

    for (uint64_t offset = 0; offset <= last; offset += BOOT_REQUEST_ALIGNMENT) {
        struct boot_request *request = (struct boot_request *)(void *)(base + offset);
        if (request->magic[0] != BOOT_REQUEST_MAGIC_LOW) continue;
        if (request->magic[1] != BOOT_REQUEST_MAGIC_HIGH) continue;

        if (answered == BOOT_MAX_REQUESTS) return -1;
        if (!answer(request, facts, arena)) return -1;
        answered++;

        /* Past this request rather than eight bytes into it: the magic cannot
           appear inside a structure that starts with it. */
        offset += sizeof(struct boot_request) - BOOT_REQUEST_ALIGNMENT;
    }
    return (int)answered;
}
