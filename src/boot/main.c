#include "acpi/acpi.h"
#include "block/ata.h"
#include "block/partition.h"
#include "boot/core.h"
#include "config/config.h"
#include "cpu/control.h"
#include "cpu/cpuid.h"
#include "elf/elf.h"
#include "fs/fat.h"
#include "log/log.h"
#include "memory/arena.h"
#include "memory/paging.h"
#include "pci/pci.h"
#include "terminal/font8x8.h"
#include "protocol/protocol.h"

#define LOADER_ARENA_BYTES (1024ULL * 1024ULL)
#define LOADER_ARENA_ALIGNMENT (4096ULL)

/* Everything below a megabyte belongs to something already. Under BIOS that is
   the boot record, our stack, the page tables and the buffer stage2 left the
   memory map in; under UEFI it is whatever the firmware kept there. Neither is
   fully described by the map. */
#define LOADER_ARENA_FLOOR (1024ULL * 1024ULL)

/* The loader may only touch what is mapped, and the first four gigabytes are
   what both paths arrive with: stage2 builds that map, and firmware provides
   one at least that large. */
#define LOADER_ADDRESS_LIMIT (4ULL * 1024ULL * 1024ULL * 1024ULL)

#define BYTES_PER_MIB (1024ULL * 1024ULL)

/* Where the boot record keeps the mark that says it is one. */
#define BOOT_RECORD_SIGNATURE_OFFSET 510U
#define BOOT_RECORD_SIGNATURE 0xAA55U

static struct memory_map memory;
static struct arena loader_arena;
static struct ata_channel boot_channel;
static struct block_device boot_disk;
static uint8_t boot_record[BLOCK_SECTOR_BYTES_DEFAULT];
static struct partition_table partitions;
static struct partition_view boot_view;
static struct block_device boot_partition;
static struct fat_volume boot_volume;

#define CONFIG_PATH "/tunix.cfg"
/* Read whole into memory: it is small, it is parsed in place, and every value
   the parser produces points back into this buffer. */
#define CONFIG_BUFFER_BYTES 4096U
#define KERNEL_PATH_BYTES 256U
#define COMMAND_LINE_BYTES 512U

static char config_text[CONFIG_BUFFER_BYTES];
static struct config config;
/* The parser hands back slices of config_text, and both of these have to be
   terminated: one to open a file with, one to hand the kernel. */
static char kernel_path[KERNEL_PATH_BYTES];
static char command_line[COMMAND_LINE_BYTES];

/* The kernel is loaded where it asked to be, and the only memory it may land in
   is memory the firmware called usable and the identity map covers. */
#define KERNEL_LIMIT_LOW (1024ULL * 1024ULL)
#define KERNEL_LIMIT_HIGH LOADER_ADDRESS_LIMIT

static struct fat_file kernel_file;
static struct page_tables kernel_tables;
static struct elf_image kernel_image;

/* The loader keeps running after the switch — its own code, stack, page tables
   and the memory it just loaded the kernel into all live down here — so the
   identity map it arrived on has to be reproduced in the kernel's tables. */
#define IDENTITY_MAP_BYTES LOADER_ADDRESS_LIMIT

static uint64_t arena_base;
static struct framebuffer screen;
static bool screen_present;
static struct terminal screen_terminal;
static struct acpi_tables acpi;
static struct acpi_processors processors;
static bool acpi_present;
static struct pci_devices pci;
/* Only once the processor has been told the bit means what we mean by it. */
static bool kernel_no_execute;

/* Enough to tell, from across a room, that the loader reached this point and
   that the mode it was told about is the mode it is drawing on: a border in
   each primary, and a bar that would be torn or sheared if the pitch or the
   channel layout were wrong. */
#define SCREEN_BORDER_PIXELS 8U
#define SCREEN_BAR_PIXELS 32U

static void paint_screen(void) {
    uint32_t width = screen.width;
    uint32_t height = screen.height;

    framebuffer_fill(&screen, 0, 0, width, height,
                     framebuffer_pack(&screen, 0x10, 0x10, 0x18));

    framebuffer_fill(&screen, 0, 0, width, SCREEN_BORDER_PIXELS,
                     framebuffer_pack(&screen, 0xFF, 0x00, 0x00));
    framebuffer_fill(&screen, 0, height - SCREEN_BORDER_PIXELS, width,
                     SCREEN_BORDER_PIXELS,
                     framebuffer_pack(&screen, 0x00, 0xFF, 0x00));
    framebuffer_fill(&screen, 0, 0, SCREEN_BORDER_PIXELS, height,
                     framebuffer_pack(&screen, 0x00, 0x00, 0xFF));
    framebuffer_fill(&screen, width - SCREEN_BORDER_PIXELS, 0,
                     SCREEN_BORDER_PIXELS, height,
                     framebuffer_pack(&screen, 0xFF, 0xFF, 0xFF));

    /* A grey ramp across the middle. Uneven steps mean the channel widths were
       read wrongly; a slanted edge means the pitch was. */
    for (uint32_t x = 0; x < width; x++) {
        uint8_t level = (uint8_t)(x * 255U / (width > 1U ? width - 1U : 1U));
        framebuffer_fill(&screen, x, height / 2U, 1, SCREEN_BAR_PIXELS,
                         framebuffer_pack(&screen, level, level, level));
    }
}

/* Reads back what was just drawn. Nobody can see the screen from a serial log,
   and a framebuffer address that is wrong, or a pitch that is, produces a
   picture that is wrong in exactly the way this catches. */
static bool screen_reads_back(void) {
    const uint32_t inset = SCREEN_BORDER_PIXELS / 2U;
    uint32_t width = screen.width;
    uint32_t height = screen.height;

    if (framebuffer_get(&screen, width / 2U, inset) !=
        framebuffer_pack(&screen, 0xFF, 0x00, 0x00)) return false;
    if (framebuffer_get(&screen, width / 2U, height - 1U - inset) !=
        framebuffer_pack(&screen, 0x00, 0xFF, 0x00)) return false;
    if (framebuffer_get(&screen, inset, height / 4U) !=
        framebuffer_pack(&screen, 0x00, 0x00, 0xFF)) return false;
    /* Sampled away from the middle, because the ramp is drawn across the whole
       width and covers the side borders on the rows it occupies. */
    if (framebuffer_get(&screen, width - 1U - inset, height / 4U) !=
        framebuffer_pack(&screen, 0xFF, 0xFF, 0xFF)) return false;

    /* The last pixel of the ramp is white and the first is black, which only
       holds if the row really is `pitch` bytes long. */
    if (framebuffer_get(&screen, width - 1U, height / 2U) !=
        framebuffer_pack(&screen, 0xFF, 0xFF, 0xFF)) return false;
    if (framebuffer_get(&screen, 0, height / 2U) !=
        framebuffer_pack(&screen, 0x00, 0x00, 0x00)) return false;
    return true;
}

/* Everything the loader logs also goes to the screen once there is one, so a
   machine that will not boot says why to whoever is looking at it rather than
   only to whoever brought a serial cable. */
static void screen_sink(const char *text) {
    terminal_write(&screen_terminal, text);
}

static void acquire_screen(const struct fw_ops *fw) {
    screen_present = fw_framebuffer_acquire(fw, &screen);
    if (!screen_present) {
        /* Not a failure: a machine with no display is one the loader has
           nothing to say to except over the serial line. */
        LOG_INFO("no framebuffer; serial only");
        return;
    }

    LOG_INFO("screen %ux%u, %u bpp, pitch %u", (uint64_t)screen.width,
             (uint64_t)screen.height, (uint64_t)screen.bits_per_pixel,
             (uint64_t)screen.pitch);
    paint_screen();
    LOG_INFO("screen readback %s", screen_reads_back() ? "ok" : "WRONG");

    if (!terminal_init(&screen_terminal, &screen, font8x8(),
                       framebuffer_pack(&screen, 0xD0, 0xD0, 0xD0),
                       framebuffer_pack(&screen, 0x10, 0x10, 0x18))) {
        LOG_INFO("the screen is too small for a terminal");
        return;
    }
    terminal_clear(&screen_terminal);
    log_add_sink(screen_sink);

    LOG_INFO("terminal %ux%u characters", (uint64_t)screen_terminal.columns,
             (uint64_t)screen_terminal.rows);
}

/* What the firmware says about the machine, so the kernel does not have to go
   looking for it a second time — and so a machine whose tables are unreadable
   says so here rather than in the kernel. */
static void describe_machine(const struct fw_ops *fw) {
    const void *rsdp = fw_rsdp_locate(fw);
    if (!rsdp) {
        LOG_INFO("no acpi tables");
        return;
    }

    acpi_present = acpi_collect(rsdp, acpi_identity_map, NULL, &acpi);
    if (!acpi_present) {
        LOG_ERROR("the acpi root table could not be believed");
        return;
    }
    LOG_INFO("acpi revision %u, %u tables%s", (uint64_t)acpi.revision,
             (uint64_t)acpi.count, acpi.truncated ? " (truncated)" : "");

    if (!acpi_processors(&acpi, &processors)) {
        LOG_INFO("no processor list");
        return;
    }

    unsigned enabled = 0;
    for (unsigned index = 0; index < processors.count; index++)
        if (processors.entries[index].enabled) enabled++;

    LOG_INFO("%u processors, %u startable, local apic at %x",
             (uint64_t)processors.count, (uint64_t)enabled,
             processors.local_apic_address);
}

static void describe_devices(void) {
    unsigned found = pci_enumerate(pci_port_config_read, NULL, &pci);
    if (found == 0) {
        LOG_INFO("no pci devices");
        return;
    }
    LOG_INFO("pci: %u devices%s", (uint64_t)found,
             pci.truncated ? " (truncated)" : "");
    for (unsigned index = 0; index < found; index++) {
        const struct pci_device *device = &pci.entries[index];
        LOG_DEBUG("  %u:%u.%u %x:%x class %x.%x", (uint64_t)device->bus,
                  (uint64_t)device->device, (uint64_t)device->function,
                  (uint64_t)device->vendor, (uint64_t)device->identifier,
                  (uint64_t)device->class_code, (uint64_t)device->subclass);
    }
}

static void report_features(const struct cpu_features *features) {
    LOG_INFO("cpu %s, %u physical / %u linear address bits",
             features->vendor,
             (uint64_t)features->physical_address_bits,
             (uint64_t)features->linear_address_bits);
    LOG_INFO("long mode %s, nx %s, 1g pages %s",
             features->long_mode ? "yes" : "no",
             features->no_execute ? "yes" : "no",
             features->gigabyte_pages ? "yes" : "no");
    LOG_INFO("smep %s, smap %s, avx %s",
             features->smep ? "yes" : "no",
             features->smap ? "yes" : "no",
             features->avx ? "yes" : "no");
}

static void report_memory(const struct memory_map *map) {
    for (size_t index = 0; index < map->count; index++) {
        const struct memory_region *region = &map->regions[index];
        LOG_DEBUG("  %x..%x %s", region->base, region->base + region->length,
                  memory_kind_name(region->kind));
    }
    LOG_INFO("memory: %u MiB usable in %u regions%s",
             memory_map_total(map, MEMORY_KIND_USABLE) / BYTES_PER_MIB,
             (uint64_t)map->count,
             map->truncated ? " (truncated)" : "");
}

static bool place_arena(const struct memory_map *map, struct arena *arena) {
    uint64_t base;
    if (!memory_map_find_free(map, LOADER_ARENA_BYTES, LOADER_ARENA_ALIGNMENT,
                              LOADER_ARENA_FLOOR, &base)) {
        return false;
    }
    if (base + LOADER_ARENA_BYTES > LOADER_ADDRESS_LIMIT) return false;

    arena_init(arena, (void *)(uintptr_t)base, LOADER_ARENA_BYTES);
    arena_base = base;
    LOG_INFO("arena at %x, %u KiB", base, LOADER_ARENA_BYTES / 1024ULL);
    return true;
}

/* ATA is driven directly rather than through the firmware. Port I/O works the
   same under BIOS and UEFI and from long mode, so this is one driver instead of
   two, and no path here has to leave long mode to read a sector. */
static bool attach_boot_disk(void) {
    if (!ata_probe(ata_port_io(), ATA_PRIMARY_IO_BASE, ATA_PRIMARY_CONTROL_BASE,
                   false, &boot_channel, &boot_disk)) {
        return false;
    }
    LOG_INFO("disk %s, %u sectors, lba48 %s", boot_disk.name,
             boot_disk.sector_count, boot_channel.lba48 ? "yes" : "no");

    if (!block_read(&boot_disk, 0, 1, boot_record)) return false;

    uint16_t signature =
        (uint16_t)(boot_record[BOOT_RECORD_SIGNATURE_OFFSET] |
                   (boot_record[BOOT_RECORD_SIGNATURE_OFFSET + 1U] << 8));
    if (signature != BOOT_RECORD_SIGNATURE) {
        LOG_ERROR("the disk read back is not the one we booted from");
        return false;
    }

    LOG_INFO("boot record verified through the ata driver");
    return true;
}

/* Mounts the first partition that turns out to hold a filesystem, rather than
   trusting the type byte: the byte is a hint that costs nothing to forge, and
   mounting is the only thing that actually answers the question. */
static bool mount_boot_filesystem(void) {
    if (!partition_table_read(&boot_disk, &partitions)) {
        LOG_ERROR("no partition table on the boot disk");
        return false;
    }
    LOG_INFO("%u partitions, %s table", (uint64_t)partitions.count,
             partitions.gpt ? "gpt" : "mbr");

    for (unsigned index = 0; index < partitions.count; index++) {
        const struct partition *entry = &partitions.entries[index];
        if (!partition_open(&boot_disk, entry, &boot_view, &boot_partition))
            continue;
        if (!fat_mount(&boot_partition, &boot_volume)) continue;

        LOG_INFO("fat32 at lba %u, %u clusters of %u bytes", entry->start_lba,
                 (uint64_t)boot_volume.cluster_count,
                 (uint64_t)boot_volume.cluster_bytes);
        return true;
    }
    LOG_ERROR("no partition held a filesystem this loader can read");
    return false;
}

static bool read_configuration(void) {
    struct fat_file file;
    if (!fat_open(&boot_volume, CONFIG_PATH, &file)) {
        LOG_ERROR("%s is not on the filesystem", CONFIG_PATH);
        return false;
    }
    if (file.size == 0 || file.size > sizeof config_text) {
        LOG_ERROR("%s is not a size a configuration should be", CONFIG_PATH);
        return false;
    }
    if (!fat_read(&file, 0, file.size, config_text)) {
        LOG_ERROR("%s would not read", CONFIG_PATH);
        return false;
    }

    struct config_error error;
    if (!config_parse(config_text, file.size, &config, &error)) {
        /* The line and the reason, because a machine that will not boot over a
           typo should say which typo. */
        LOG_ERROR("%s line %u: %s", CONFIG_PATH, error.line, error.reason);
        return false;
    }

    const struct config_entry *entry = config_default_entry(&config);
    if (!entry) {
        LOG_ERROR("%s names no entry to boot", CONFIG_PATH);
        return false;
    }

    if (!str_copy_cstr(entry->kernel, kernel_path, sizeof kernel_path)) {
        LOG_ERROR("the kernel path is longer than this loader can hold");
        return false;
    }
    if (!str_copy_cstr(entry->cmdline, command_line, sizeof command_line)) {
        LOG_ERROR("the command line is longer than this loader can hold");
        return false;
    }

    LOG_INFO("config: %u entries, timeout %u, booting %s",
             (uint64_t)config.entry_count, config.timeout_seconds, kernel_path);
    LOG_INFO("cmdline from config: %s", command_line);
    return true;
}

/* The ELF reader speaks to whatever the kernel happens to live on. */
static bool kernel_read(void *context, uint64_t offset, size_t length,
                        void *out) {
    return fat_read((struct fat_file *)context, offset, length, out);
}

/* Nothing here checks that the kernel's segments miss the loader's own memory:
   the loader sits below a megabyte and the window starts at one, so a segment
   that would reach it is refused by elf_load before anything moves. */
static bool load_kernel(uint64_t *entry) {
    if (!fat_open(&boot_volume, kernel_path, &kernel_file)) {
        LOG_ERROR("%s is not on the filesystem", kernel_path);
        return false;
    }

    if (!elf_parse(kernel_read, &kernel_file, &kernel_image)) {
        LOG_ERROR("%s is not an elf64 kernel this loader can run", kernel_path);
        return false;
    }
    LOG_INFO("kernel %u segments, %x..%x, entry %x",
             (uint64_t)kernel_image.segment_count, kernel_image.lowest_address,
             kernel_image.highest_address, kernel_image.entry);

    /* Every segment must fall in memory the firmware called usable. Checking
       the span is not enough on its own, but a span that fails is decisive. */
    const struct memory_region *region =
        memory_map_find(&memory, kernel_image.lowest_address);
    if (!region || region->kind != MEMORY_KIND_USABLE ||
        kernel_image.highest_address > region->base + region->length) {
        LOG_ERROR("the kernel does not fit in one run of usable memory");
        return false;
    }

    struct elf_placement placement = {
        .bias = 0,
        .limit_low = KERNEL_LIMIT_LOW,
        .limit_high = KERNEL_LIMIT_HIGH,
    };
    if (!elf_load(kernel_read, &kernel_file, &kernel_image, &placement)) {
        LOG_ERROR("the kernel would not load where it asked to be");
        return false;
    }

    *entry = elf_entry_point(&kernel_image, &placement);
    if (*entry == 0) return false;

    LOG_INFO("kernel loaded, entering at %x", *entry);
    return true;
}

/* A segment's own pages, rounded out at both ends: the mapping is by page and a
   segment rarely starts or ends on one. */
static bool map_segment(const struct elf_segment *segment, bool writable) {
    uint64_t virtual = segment->virtual_address & ~(PAGE_BYTES - 1U);
    uint64_t physical = segment->physical_address & ~(PAGE_BYTES - 1U);
    uint64_t leading = segment->virtual_address - virtual;

    uint64_t bytes = segment->memory_bytes + leading;
    bytes = (bytes + PAGE_BYTES - 1U) & ~(PAGE_BYTES - 1U);

    uint64_t flags = writable ? PAGE_WRITABLE : 0;
    /* Only when the processor has been told the bit means what we mean by it;
       otherwise bit 63 is reserved and the page faults on its first read. */
    if (!(segment->flags & ELF_FLAG_EXECUTE) && kernel_no_execute) {
        flags |= PAGE_NO_EXECUTE;
    }

    return paging_map(&kernel_tables, virtual, physical, bytes, flags);
}

static bool build_kernel_tables(const struct elf_image *kernel,
                                const struct cpu_features *features) {
    /* Before any table carries it: without this, bit 63 is a reserved bit and a
       page marked no-execute faults on the first read, not the first jump. */
    kernel_no_execute = features->no_execute;
    if (kernel_no_execute) control_enable_no_execute();

    if (!paging_create(&kernel_tables, &loader_arena, features->gigabyte_pages))
        return false;

    if (!paging_map(&kernel_tables, 0, 0, IDENTITY_MAP_BYTES, PAGE_WRITABLE)) {
        LOG_ERROR("could not identity map low memory");
        return false;
    }

    for (unsigned index = 0; index < kernel->segment_count; index++) {
        const struct elf_segment *segment = &kernel->segments[index];
        /* A segment already reachable at its own address needs nothing more;
           mapping it twice would be refused as a remap. */
        if (segment->virtual_address == segment->physical_address) continue;

        if (!map_segment(segment, (segment->flags & ELF_FLAG_WRITE) != 0)) {
            LOG_ERROR("could not map a kernel segment at %x",
                      segment->virtual_address);
            return false;
        }
    }

    unsigned executable = 0, writable = 0;
    for (unsigned index = 0; index < kernel->segment_count; index++) {
        if (kernel->segments[index].flags & ELF_FLAG_EXECUTE) executable++;
        if (kernel->segments[index].flags & ELF_FLAG_WRITE) writable++;
    }

    LOG_INFO("page tables at %x, %u KiB of them", kernel_tables.root,
             arena_used(&loader_arena) / 1024ULL);
    /* No segment should be both, and everything that is not executable is
       marked so in the tables — which only means anything with NX enabled. */
    LOG_INFO("kernel mapping: %u executable, %u writable, nx %s",
             (uint64_t)executable, (uint64_t)writable,
             kernel_no_execute ? "on" : "off");
    return true;
}

/* The firmware map describes the machine, not what the loader has since done to
   it. Adding these makes the map the kernel is handed the truth: the more
   restrictive kind wins where they overlap usable memory, so the carve-out
   happens on its own. */
static void claim_loader_memory(const struct elf_image *kernel) {
    memory_map_add(&memory, arena_base, LOADER_ARENA_BYTES,
                   MEMORY_KIND_BOOTLOADER_RECLAIMABLE);
    memory_map_add(&memory, kernel->lowest_address,
                   kernel->highest_address - kernel->lowest_address,
                   MEMORY_KIND_KERNEL);
    memory_map_finalize(&memory);

    LOG_INFO("after claiming: %u MiB usable in %u regions",
             memory_map_total(&memory, MEMORY_KIND_USABLE) / BYTES_PER_MIB,
             (uint64_t)memory.count);
}

/* Answers are written into the kernel's image, which is only reachable while
   the loader is still identity mapped — so this happens before the switch. */
static bool answer_kernel_requests(const struct elf_image *kernel) {
    struct boot_facts facts = {
        .memory = &memory,
        .kernel_physical_base = kernel->lowest_address,
        .kernel_virtual_base = kernel->segments[0].virtual_address,
        .command_line = command_line,
        .screen = screen_present ? &screen : NULL,
        .rsdp = acpi_present ? acpi.rsdp : NULL,
        .processors = processors.count > 0 ? &processors : NULL,
    };

    int answered = protocol_answer(
        (void *)(uintptr_t)kernel->lowest_address,
        kernel->highest_address - kernel->lowest_address, &facts, &loader_arena);

    if (answered < 0) {
        LOG_ERROR("could not answer the kernel's requests");
        return false;
    }
    LOG_INFO("answered %u kernel requests", (uint64_t)answered);
    return true;
}

void boot_core(const struct fw_ops *fw) {
    LOG_INFO("tunix-boot on %s firmware", fw_name(fw));

    struct cpu_features features;
    cpu_features_detect(cpuid_execute, &features);
    report_features(&features);

    if (!features.long_mode) {
        LOG_ERROR("the processor does not report long mode");
        return;
    }

    if (!fw_mem_snapshot(fw, &memory)) {
        LOG_ERROR("the firmware described no usable memory");
        return;
    }
    report_memory(&memory);

    acquire_screen(fw);

    describe_machine(fw);
    describe_devices();

    if (!place_arena(&memory, &loader_arena)) {
        LOG_ERROR("no room for the loader arena");
        return;
    }

    if (!attach_boot_disk()) {
        LOG_ERROR("no readable boot disk");
        return;
    }

    if (!mount_boot_filesystem()) return;
    if (!read_configuration()) return;

    LOG_INFO("the machine is described and readable");

    uint64_t entry;
    if (!load_kernel(&entry)) return;

    claim_loader_memory(&kernel_image);
    if (!answer_kernel_requests(&kernel_image)) return;

    if (!build_kernel_tables(&kernel_image, &features)) return;

    /* From here the kernel's map is the one in force. The loader survives it
       only because that map identity maps everything the loader is standing on. */
    paging_activate(&kernel_tables);
    LOG_INFO("kernel page tables active, entering at %x", entry);

    /* No handoff protocol yet: the kernel is entered with nothing but the
       machine state established here. That is M9. */
    ((void (*)(void))(uintptr_t)entry)();
    LOG_ERROR("the kernel returned");
}
