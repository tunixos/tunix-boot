#ifndef TUNIX_BOOT_FW_UEFI_UEFI_H
#define TUNIX_BOOT_FW_UEFI_UEFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Just enough of the UEFI tables to get the memory map and leave.
 *
 * The loader drives the disk itself and prints over the serial port, so the
 * firmware is needed for exactly two things: describing memory, and being told
 * to stop. Declaring only that keeps this header something a person can read,
 * and keeps the surface the firmware can be wrong about small.
 *
 * Every function here is called through a pointer the firmware supplies, so the
 * signatures have to be right about the calling convention. Compiled with
 * -target x86_64-unknown-windows the MS ABI is already the default, which is
 * why nothing is annotated: a lone annotation would be the thing that is wrong
 * if the target ever changed.
 */

typedef uint64_t efi_status;
typedef void *efi_handle;

#define EFI_SUCCESS 0ULL
/* The high bit marks an error, so a status is not a small number. */
#define EFI_ERROR_BIT (1ULL << 63)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5ULL)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2ULL)

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL

/* AllocatePages types and memory type for the loader's own allocations. */
#define EFI_ALLOCATE_ANY_PAGES 0U
#define EFI_LOADER_DATA 2U

struct efi_guid;

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_bytes;
    uint32_t crc32;
    uint32_t reserved;
};

struct efi_simple_text_output {
    void *reset;
    efi_status (*output_string)(struct efi_simple_text_output *self,
                                const uint16_t *text);
};

/*
 * The boot services table, declared only as far as the calls this loader makes.
 * The entries before each one are padding rather than named members, because
 * naming a function this code never calls is claiming to know a signature it
 * has no way to be right about.
 */
struct efi_boot_services {
    struct efi_table_header header;

    void *raise_tpl;
    void *restore_tpl;

    efi_status (*allocate_pages)(uint32_t type, uint32_t memory_type,
                                 uint64_t pages, uint64_t *memory);
    efi_status (*free_pages)(uint64_t memory, uint64_t pages);
    efi_status (*get_memory_map)(uint64_t *map_bytes, void *map,
                                 uint64_t *map_key, uint64_t *descriptor_bytes,
                                 uint32_t *descriptor_version);
    efi_status (*allocate_pool)(uint32_t pool_type, uint64_t bytes,
                                void **buffer);
    efi_status (*free_pool)(void *buffer);

    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    void *handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit_image;
    void *unload_image;

    efi_status (*exit_boot_services)(efi_handle image, uint64_t map_key);

    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;

    efi_status (*locate_protocol)(const struct efi_guid *protocol,
                                  void *registration, void **interface);
};

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                     \
    {0x9042A9DE, 0x23DC, 0x4A38, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}}

struct efi_graphics_mode_information {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    /* Red, green, blue, reserved — only meaningful when the format says so. */
    uint32_t pixel_masks[4];
    uint32_t pixels_per_scanline;
};

struct efi_graphics_mode {
    uint32_t max_mode;
    uint32_t mode;
    struct efi_graphics_mode_information *info;
    uint64_t info_bytes;
    uint64_t framebuffer_base;
    uint64_t framebuffer_bytes;
};

struct efi_graphics_output {
    void *query_mode;
    void *set_mode;
    void *blt;
    struct efi_graphics_mode *mode;
};

#define EFI_ACPI_20_TABLE_GUID                                                \
    {0x8868E871, 0xE4F1, 0x11D3, {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}}

struct efi_configuration_table {
    struct efi_guid vendor_guid;
    void *vendor_table;
};

struct efi_system_table {
    struct efi_table_header header;
    const uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    efi_handle console_in_handle;
    void *console_in;
    efi_handle console_out_handle;
    struct efi_simple_text_output *console_out;
    efi_handle standard_error_handle;
    struct efi_simple_text_output *standard_error;
    void *runtime_services;
    struct efi_boot_services *boot_services;
    uint64_t configuration_table_count;
    struct efi_configuration_table *configuration_table;
};

/* Room for the map plus what GetMemoryMap may grow by between the call that
   sizes it and the call that fills it — allocating in between changes it. */
#define UEFI_MEMORY_MAP_SLACK_BYTES 4096U
#define UEFI_MEMORY_MAP_MAX_BYTES 32768U

/* True when the system table is one, rather than whatever was in the register.
   A firmware that fails this is one nothing below should be talking to. */
bool uefi_system_table_valid(const struct efi_system_table *table);

#endif
