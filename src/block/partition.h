#ifndef TUNIX_BOOT_BLOCK_PARTITION_H
#define TUNIX_BOOT_BLOCK_PARTITION_H

#include <stdbool.h>
#include <stdint.h>

#include "block/block.h"

/*
 * MBR and GPT partition tables.
 *
 * A partition is where a filesystem starts, and every offset a filesystem
 * computes is relative to it. Getting that base wrong reads plausible garbage
 * rather than failing, so the table is parsed once, here, with every field
 * checked against the size of the disk it claims to describe.
 */

#define MBR_SIGNATURE_OFFSET 510U
#define MBR_SIGNATURE 0xAA55U
#define MBR_TABLE_OFFSET 446U
#define MBR_ENTRY_BYTES 16U
#define MBR_ENTRY_COUNT 4U

#define MBR_ENTRY_STATUS_OFFSET 0U
#define MBR_ENTRY_TYPE_OFFSET 4U
#define MBR_ENTRY_START_LBA_OFFSET 8U
#define MBR_ENTRY_SECTORS_OFFSET 12U

#define MBR_STATUS_ACTIVE 0x80U
#define MBR_TYPE_EMPTY 0x00U
/* The whole disk is one GPT partition wearing an MBR disguise, so that tools
   which only know MBR do not decide the disk is unpartitioned and helpfully
   reformat it. Seeing this means the real table is a GPT. */
#define MBR_TYPE_GPT_PROTECTIVE 0xEEU

#define GPT_HEADER_LBA 1U
#define GPT_SIGNATURE "EFI PART"
#define GPT_SIGNATURE_BYTES 8U
#define GPT_HEADER_ENTRY_LBA_OFFSET 72U
#define GPT_HEADER_ENTRY_COUNT_OFFSET 80U
#define GPT_HEADER_ENTRY_BYTES_OFFSET 84U
#define GPT_ENTRY_TYPE_OFFSET 0U
#define GPT_ENTRY_FIRST_LBA_OFFSET 32U
#define GPT_ENTRY_LAST_LBA_OFFSET 40U
#define GPT_ENTRY_MIN_BYTES 128U
#define GPT_ENTRY_MAX_BYTES 4096U

#define PARTITION_MAX 16U

struct partition {
    uint64_t start_lba;
    uint64_t sector_count;
    /* The MBR type byte, or zero for GPT entries. Kept because it is the only
       cheap hint about what filesystem to expect; nothing here trusts it. */
    uint8_t type;
    bool active;
};

struct partition_table {
    struct partition entries[PARTITION_MAX];
    unsigned count;
    bool gpt;
};

/* Reads the table from the device. False when there is none that can be
   believed; individually impossible entries are skipped and the rest kept. */
bool partition_table_read(struct block_device *device,
                          struct partition_table *out);

/* Parses an in-memory MBR sector. Separate from the read so the tests can hand
   it sectors no disk would produce. */
bool partition_parse_mbr(const uint8_t *sector, uint64_t device_sectors,
                         struct partition_table *out);

struct partition_view {
    struct block_device *parent;
    uint64_t start_lba;
};

/* A device confined to one partition, so everything above addresses the
   filesystem from zero and cannot reach past it. The view holds the state the
   returned device reads through and must outlive it. */
bool partition_open(struct block_device *device, const struct partition *entry,
                    struct partition_view *view, struct block_device *out);

#endif
