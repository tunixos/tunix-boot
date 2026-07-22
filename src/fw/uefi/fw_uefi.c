#include "acpi/acpi.h"
#include "fw/uefi/graphics.h"
#include "fw/uefi/memory.h"
#include "fw/uefi/uefi.h"
#include "fw/fw.h"
#include "util/mem.h"

#define FW_UEFI_NAME "uefi"

static const struct efi_guid graphics_output_guid =
    EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static const struct efi_guid acpi_table_guid = EFI_ACPI_20_TABLE_GUID;

static bool same_guid(const struct efi_guid *left, const struct efi_guid *right) {
    return memcmp(left, right, sizeof *left) == 0;
}

bool uefi_system_table_valid(const struct efi_system_table *table) {
    if (!table) return false;
    if (table->header.signature != EFI_SYSTEM_TABLE_SIGNATURE) return false;
    if (!table->boot_services) return false;
    if (!table->boot_services->get_memory_map) return false;
    if (!table->boot_services->exit_boot_services) return false;
    return true;
}

/* ops first, so a struct fw_ops * can be cast back to this. */
struct fw_uefi {
    struct fw_ops ops;
    struct efi_system_table *system;
    efi_handle image;
    /* The key GetMemoryMap returned last. ExitBootServices refuses a key that
       is not current, which is the firmware's way of saying the map changed
       under us since we looked. */
    uint64_t map_key;
    bool exited;
    uint8_t map[UEFI_MEMORY_MAP_MAX_BYTES];
};

static struct fw_uefi firmware;

static bool fetch_memory_map(struct fw_uefi *self, uint64_t *map_bytes,
                             uint64_t *descriptor_bytes) {
    struct efi_boot_services *services = self->system->boot_services;

    /* Asked for the size first, then given more than that: allocating the
       buffer can itself add a descriptor, and a map that no longer fits is a
       map the second call refuses to write. */
    uint64_t needed = 0;
    uint64_t key = 0;
    uint64_t stride = 0;
    uint32_t version = 0;
    (void)services->get_memory_map(&needed, NULL, &key, &stride, &version);

    if (needed == 0) return false;
    if (needed + UEFI_MEMORY_MAP_SLACK_BYTES > sizeof self->map) return false;

    uint64_t capacity = sizeof self->map;
    efi_status status = services->get_memory_map(&capacity, self->map, &key,
                                                 &stride, &version);
    if (status != EFI_SUCCESS) return false;

    self->map_key = key;
    *map_bytes = capacity;
    *descriptor_bytes = stride;
    return true;
}

static bool uefi_mem_snapshot(const struct fw_ops *fw, struct memory_map *out) {
    struct fw_uefi *self = (struct fw_uefi *)(void *)(uintptr_t)fw;

    uint64_t map_bytes, descriptor_bytes;
    if (!fetch_memory_map(self, &map_bytes, &descriptor_bytes)) return false;

    /* Before the exit the firmware is still running out of its own memory, so
       what is usable depends on which side of it we are. */
    enum uefi_phase phase = self->exited ? UEFI_AFTER_EXIT : UEFI_BEFORE_EXIT;
    return uefi_memory_parse(self->map, (size_t)map_bytes,
                             (size_t)descriptor_bytes, phase, out);
}

static bool uefi_framebuffer_acquire(const struct fw_ops *fw,
                                     struct framebuffer *out) {
    struct fw_uefi *self = (struct fw_uefi *)(void *)(uintptr_t)fw;
    struct efi_boot_services *services = self->system->boot_services;

    /* Only while boot services are up: the protocol is one of the things that
       goes away with them, which is why this is not part of the snapshot. */
    if (self->exited || !services->locate_protocol) return false;

    struct efi_graphics_output *graphics = NULL;
    if (services->locate_protocol(&graphics_output_guid, NULL,
                                  (void **)&graphics) != EFI_SUCCESS) {
        return false;
    }
    /* A machine with no display is not a failure to boot; it is a machine the
       loader has nothing to say to except over the serial line. */
    if (!graphics || !graphics->mode || !graphics->mode->info) return false;

    const struct efi_graphics_mode_information *info = graphics->mode->info;
    struct uefi_pixel_masks masks = {
        .red = info->pixel_masks[0],
        .green = info->pixel_masks[1],
        .blue = info->pixel_masks[2],
        .reserved = info->pixel_masks[3],
    };

    return uefi_graphics_describe(info->pixel_format, &masks,
                                  info->horizontal_resolution,
                                  info->vertical_resolution,
                                  info->pixels_per_scanline,
                                  graphics->mode->framebuffer_base, out);
}

/* The firmware lists it outright, so there is nothing to search for — and the
   entry it lists is only worth using if it really is an RSDP. */
static const void *uefi_rsdp_locate(const struct fw_ops *fw) {
    const struct fw_uefi *self = (const struct fw_uefi *)fw;
    const struct efi_system_table *system = self->system;

    if (!system->configuration_table) return NULL;
    for (uint64_t index = 0; index < system->configuration_table_count; index++) {
        const struct efi_configuration_table *entry =
            &system->configuration_table[index];
        if (!same_guid(&entry->vendor_guid, &acpi_table_guid)) continue;
        if (acpi_rsdp_valid(entry->vendor_table)) return entry->vendor_table;
    }
    return NULL;
}

/*
 * Leaving boot services. The map has to be re-read immediately beforehand,
 * because ExitBootServices only accepts the key from a map that is still
 * current — and anything the loader allocated since makes it stale. The
 * firmware is allowed to fail this once for exactly that reason, so it is worth
 * one retry and no more.
 */
bool uefi_exit_boot_services(struct memory_map *out);

bool uefi_exit_boot_services(struct memory_map *out) {
    struct fw_uefi *self = &firmware;
    if (self->exited) return false;

    struct efi_boot_services *services = self->system->boot_services;
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        uint64_t map_bytes, descriptor_bytes;
        if (!fetch_memory_map(self, &map_bytes, &descriptor_bytes)) return false;

        if (services->exit_boot_services(self->image, self->map_key) !=
            EFI_SUCCESS) {
            continue;
        }

        /* From here there is no firmware to call. The map in hand is the one
           the kernel is handed, and boot services memory is now free. */
        self->exited = true;
        return uefi_memory_parse(self->map, (size_t)map_bytes,
                                 (size_t)descriptor_bytes, UEFI_AFTER_EXIT, out);
    }
    return false;
}

const struct fw_ops *fw_uefi_init(efi_handle image,
                                  struct efi_system_table *system);

const struct fw_ops *fw_uefi_init(efi_handle image,
                                  struct efi_system_table *system) {
    if (!uefi_system_table_valid(system)) return NULL;

    firmware.ops.name = FW_UEFI_NAME;
    firmware.ops.mem_snapshot = uefi_mem_snapshot;
    firmware.ops.framebuffer_acquire = uefi_framebuffer_acquire;
    firmware.ops.rsdp_locate = uefi_rsdp_locate;
    firmware.system = system;
    firmware.image = image;
    firmware.exited = false;
    return &firmware.ops;
}
