#include "acpi/acpi.h"
#include "util/mem.h"
#include "util/reader.h"

static uint32_t table_length(const void *table) {
    const uint8_t *at = (const uint8_t *)table + ACPI_SDT_LENGTH_OFFSET;
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

bool acpi_checksum(const void *bytes, size_t length) {
    const uint8_t *at = (const uint8_t *)bytes;
    uint8_t sum = 0;
    for (size_t index = 0; index < length; index++) sum = (uint8_t)(sum + at[index]);
    return sum == 0;
}

bool acpi_rsdp_valid(const void *candidate) {
    if (!candidate) return false;
    if (memcmp(candidate, ACPI_RSDP_SIGNATURE, ACPI_RSDP_SIGNATURE_BYTES) != 0)
        return false;

    /* The first twenty bytes always, whatever the revision says. */
    if (!acpi_checksum(candidate, ACPI_RSDP_REVISION_1_BYTES)) return false;

    const uint8_t *at = (const uint8_t *)candidate;
    if (at[ACPI_RSDP_REVISION_OFFSET] < ACPI_RSDP_REVISION_2) return true;

    const uint8_t *length_at = at + ACPI_RSDP_LENGTH_OFFSET;
    uint32_t length = (uint32_t)length_at[0] | ((uint32_t)length_at[1] << 8) |
                      ((uint32_t)length_at[2] << 16) |
                      ((uint32_t)length_at[3] << 24);

    /* A length outside these makes the second checksum a walk over memory that
       has nothing to do with the table. */
    if (length < ACPI_RSDP_REVISION_1_BYTES) return false;
    if (length > ACPI_RSDP_MAX_BYTES) return false;
    return acpi_checksum(candidate, length);
}

const void *acpi_find_rsdp(const void *region, size_t length) {
    if (!region || length < ACPI_RSDP_REVISION_1_BYTES) return NULL;

    const uint8_t *base = (const uint8_t *)region;
    size_t last = length - ACPI_RSDP_REVISION_1_BYTES;

    for (size_t offset = 0; offset <= last; offset += ACPI_RSDP_ALIGNMENT) {
        if (acpi_rsdp_valid(base + offset)) return base + offset;
    }
    return NULL;
}

/* A table is worth keeping only if its own header says a length this code can
   believe and the bytes at it add to zero. */
static bool table_usable(const void *table) {
    if (!table) return false;

    uint32_t length = table_length(table);
    if (length < ACPI_SDT_HEADER_BYTES) return false;
    if (length > ACPI_SDT_MAX_BYTES) return false;
    return acpi_checksum(table, length);
}

static void keep(struct acpi_tables *out, const void *table) {
    if (!table_usable(table)) return;
    if (out->count == ACPI_MAX_TABLES) {
        out->truncated = true;
        return;
    }
    out->entries[out->count++] = table;
}

const void *acpi_identity_map(uint64_t physical, void *context) {
    (void)context;
    return (const void *)(uintptr_t)physical;
}

bool acpi_collect(const void *rsdp, acpi_map_fn map, void *context,
                  struct acpi_tables *out) {
    if (!out || !map) return false;

    out->count = 0;
    out->truncated = false;
    out->rsdp = NULL;
    out->revision = 0;

    if (!acpi_rsdp_valid(rsdp)) return false;

    const uint8_t *at = (const uint8_t *)rsdp;
    out->rsdp = rsdp;
    out->revision = at[ACPI_RSDP_REVISION_OFFSET];

    struct reader reader;
    reader_init(&reader, rsdp, ACPI_RSDP_MAX_BYTES);

    /* The XSDT when the firmware offers one: its entries are 64-bit, and on a
       machine with tables above four gigabytes the RSDT cannot name them. */
    const void *root = NULL;
    bool wide = false;

    if (out->revision >= ACPI_RSDP_REVISION_2) {
        uint64_t xsdt;
        if (reader_seek(&reader, ACPI_RSDP_XSDT_OFFSET) &&
            reader_u64(&reader, &xsdt) && xsdt != 0) {
            root = map(xsdt, context);
            wide = true;
        }
    }
    if (!root) {
        uint32_t rsdt;
        if (!reader_seek(&reader, ACPI_RSDP_RSDT_OFFSET)) return false;
        if (!reader_u32(&reader, &rsdt) || rsdt == 0) return false;
        root = map(rsdt, context);
        wide = false;
    }

    if (!table_usable(root)) return false;

    uint32_t length = table_length(root);
    uint32_t stride = wide ? 8U : 4U;
    uint32_t count = (length - ACPI_SDT_HEADER_BYTES) / stride;

    struct reader entries;
    reader_init(&entries, root, length);
    if (!reader_seek(&entries, ACPI_SDT_HEADER_BYTES)) return false;

    for (uint32_t index = 0; index < count; index++) {
        uint64_t pointer = 0;
        if (wide) {
            if (!reader_u64(&entries, &pointer)) break;
        } else {
            uint32_t narrow;
            if (!reader_u32(&entries, &narrow)) break;
            pointer = narrow;
        }
        if (pointer == 0) continue;
        keep(out, map(pointer, context));
    }
    return true;
}

const void *acpi_find_table(const struct acpi_tables *tables,
                            const char *signature) {
    if (!tables || !signature) return NULL;

    for (unsigned index = 0; index < tables->count; index++) {
        if (memcmp(tables->entries[index], signature,
                   ACPI_SDT_SIGNATURE_BYTES) == 0) {
            return tables->entries[index];
        }
    }
    return NULL;
}

static void add_processor(struct acpi_processors *out, uint32_t apic_id,
                          uint32_t flags) {
    if (out->count == ACPI_MAX_PROCESSORS) {
        out->truncated = true;
        return;
    }
    out->entries[out->count].apic_id = apic_id;
    /* Online-capable means it may be started later; both count as processors
       that exist, and only the flag says which may be woken now. */
    out->entries[out->count].enabled =
        (flags & (ACPI_MADT_PROCESSOR_ENABLED |
                  ACPI_MADT_PROCESSOR_ONLINE_CAPABLE)) != 0;
    out->count++;
}

bool acpi_processors(const struct acpi_tables *tables,
                     struct acpi_processors *out) {
    if (!out) return false;

    out->count = 0;
    out->truncated = false;
    out->local_apic_address = 0;

    const void *madt = acpi_find_table(tables, ACPI_SIGNATURE_MADT);
    if (!madt) return false;

    uint32_t length = table_length(madt);
    if (length < ACPI_MADT_ENTRIES_OFFSET) return false;

    struct reader reader;
    reader_init(&reader, madt, length);

    uint32_t local_apic;
    if (!reader_seek(&reader, ACPI_MADT_LOCAL_APIC_OFFSET)) return false;
    if (!reader_u32(&reader, &local_apic)) return false;
    out->local_apic_address = local_apic;

    uint32_t offset = ACPI_MADT_ENTRIES_OFFSET;
    while (offset + ACPI_MADT_ENTRY_MIN_BYTES <= length) {
        const uint8_t *entry = (const uint8_t *)madt + offset;
        uint8_t type = entry[ACPI_MADT_ENTRY_TYPE_OFFSET];
        uint8_t entry_bytes = entry[ACPI_MADT_ENTRY_LENGTH_OFFSET];

        /* A record that does not advance is a list that never ends, and one
           longer than the table runs off the end of it. */
        if (entry_bytes < ACPI_MADT_ENTRY_MIN_BYTES) break;
        if (offset + entry_bytes > length) break;

        struct reader inner;
        reader_init(&inner, entry, entry_bytes);

        if (type == ACPI_MADT_LOCAL_APIC) {
            uint8_t apic_id;
            uint32_t flags;
            if (reader_seek(&inner, ACPI_MADT_LOCAL_APIC_ID_OFFSET) &&
                reader_u8(&inner, &apic_id) &&
                reader_seek(&inner, ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET) &&
                reader_u32(&inner, &flags)) {
                add_processor(out, apic_id, flags);
            }
        } else if (type == ACPI_MADT_LOCAL_X2APIC) {
            uint32_t apic_id, flags;
            if (reader_seek(&inner, ACPI_MADT_X2APIC_ID_OFFSET) &&
                reader_u32(&inner, &apic_id) &&
                reader_seek(&inner, ACPI_MADT_X2APIC_FLAGS_OFFSET) &&
                reader_u32(&inner, &flags)) {
                add_processor(out, apic_id, flags);
            }
        }

        offset += entry_bytes;
    }
    return out->count > 0;
}
