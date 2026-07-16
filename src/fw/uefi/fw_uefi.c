#include "fw/uefi/memory.h"
#include "fw/uefi/uefi.h"
#include "fw/fw.h"

#define FW_UEFI_NAME "uefi"

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
    firmware.system = system;
    firmware.image = image;
    firmware.exited = false;
    return &firmware.ops;
}
