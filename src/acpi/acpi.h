#ifndef TUNIX_BOOT_ACPI_ACPI_H
#define TUNIX_BOOT_ACPI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Finding the firmware's description of the machine.
 *
 * ACPI is a pointer to a pointer to a list of tables, every one of which is
 * checksummed, and every one of which came from firmware. The checksums are the
 * only thing standing between a mis-scanned address and a walk through whatever
 * happens to be at it, so nothing here is used before its checksum passes.
 *
 * Physical addresses are used directly as pointers, which holds because the
 * loader runs identity mapped — and in the tests, where the tables are built in
 * host memory and the addresses in them are host pointers.
 */

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDP_SIGNATURE_BYTES 8U
/* The signature is aligned to this wherever it is, which is what makes scanning
   for it cheap enough to do over a whole segment. */
#define ACPI_RSDP_ALIGNMENT 16U

#define ACPI_RSDP_CHECKSUM_OFFSET 8U
#define ACPI_RSDP_REVISION_OFFSET 15U
#define ACPI_RSDP_RSDT_OFFSET 16U
#define ACPI_RSDP_LENGTH_OFFSET 20U
#define ACPI_RSDP_XSDT_OFFSET 24U

/* Revision 0 is the original twenty-byte structure; 2 added the extended half
   and a second checksum over the whole of it. */
#define ACPI_RSDP_REVISION_1_BYTES 20U
#define ACPI_RSDP_REVISION_2 2U
#define ACPI_RSDP_MAX_BYTES 64U

#define ACPI_SDT_HEADER_BYTES 36U
#define ACPI_SDT_SIGNATURE_BYTES 4U
#define ACPI_SDT_LENGTH_OFFSET 4U

/* Sanity bounds on a length that came off the firmware and sizes a checksum
   loop over memory. */
#define ACPI_SDT_MAX_BYTES (1024U * 1024U)

#define ACPI_MAX_TABLES 64U

#define ACPI_SIGNATURE_MADT "APIC"
#define ACPI_SIGNATURE_FADT "FACP"
#define ACPI_SIGNATURE_MCFG "MCFG"

/* Multiple APIC Description Table: where the processors are listed. */
#define ACPI_MADT_LOCAL_APIC_OFFSET 36U
#define ACPI_MADT_FLAGS_OFFSET 40U
#define ACPI_MADT_ENTRIES_OFFSET 44U

#define ACPI_MADT_ENTRY_TYPE_OFFSET 0U
#define ACPI_MADT_ENTRY_LENGTH_OFFSET 1U
#define ACPI_MADT_ENTRY_MIN_BYTES 2U

#define ACPI_MADT_LOCAL_APIC 0U
#define ACPI_MADT_IO_APIC 1U
#define ACPI_MADT_LOCAL_X2APIC 9U

#define ACPI_MADT_LOCAL_APIC_ID_OFFSET 3U
#define ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET 4U
#define ACPI_MADT_X2APIC_ID_OFFSET 4U
#define ACPI_MADT_X2APIC_FLAGS_OFFSET 8U

/* Bit 0 of a processor's flags. A processor that is not enabled and not
   online-capable is one the firmware is telling us not to start. */
#define ACPI_MADT_PROCESSOR_ENABLED 0x1U
#define ACPI_MADT_PROCESSOR_ONLINE_CAPABLE 0x2U

#define ACPI_MAX_PROCESSORS 256U

struct acpi_tables {
    /* Every table the root pointer listed and whose checksum passed. */
    const void *entries[ACPI_MAX_TABLES];
    unsigned count;
    const void *rsdp;
    uint8_t revision;
    bool truncated;
};

struct acpi_processor {
    uint32_t apic_id;
    bool enabled;
};

struct acpi_processors {
    struct acpi_processor entries[ACPI_MAX_PROCESSORS];
    unsigned count;
    uint64_t local_apic_address;
    bool truncated;
};

/* Sums `length` bytes and reports whether they come to zero, which is what
   every ACPI checksum means. */
bool acpi_checksum(const void *bytes, size_t length);

/* True when `candidate` is an RSDP: the signature, and the checksum over the
   right number of bytes for the revision it claims. */
bool acpi_rsdp_valid(const void *candidate);

/* Scans a region for one. Used on the BIOS path, where there is nobody to ask;
   under UEFI the firmware hands the address over and this is not needed. */
const void *acpi_find_rsdp(const void *region, size_t length);

/* Turns a physical address into something this code can read. On the target the
   loader is identity mapped and this is acpi_identity_map; the tests use it to
   reach tables in host memory, which the RSDT cannot name — its entries are
   32-bit, and a host pointer does not fit in one. */
typedef const void *(*acpi_map_fn)(uint64_t physical, void *context);

const void *acpi_identity_map(uint64_t physical, void *context);

/* Walks the XSDT if there is one, the RSDT otherwise, keeping the tables whose
   checksums pass. False when the root itself cannot be believed. */
bool acpi_collect(const void *rsdp, acpi_map_fn map, void *context,
                  struct acpi_tables *out);

const void *acpi_find_table(const struct acpi_tables *tables,
                            const char *signature);

/* The processors the MADT lists. False when there is no MADT or it is not one
   this can read. */
bool acpi_processors(const struct acpi_tables *tables,
                     struct acpi_processors *out);

#endif
