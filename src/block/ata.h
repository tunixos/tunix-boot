#ifndef TUNIX_BOOT_BLOCK_ATA_H
#define TUNIX_BOOT_BLOCK_ATA_H

#include <stdbool.h>
#include <stdint.h>

#include "block/block.h"

/*
 * ATA in programmed I/O, driven from long mode.
 *
 * The alternative was a real-mode thunk around INT 13h, which means leaving long
 * mode and coming back for every read. Port I/O needs neither, and unlike the
 * firmware service it behaves the same under BIOS and UEFI.
 *
 * Every port access goes through struct ata_io, so the state machine — which is
 * the part that can be wrong — is driven against a simulated drive in the tests
 * rather than only against real hardware.
 */

#define ATA_PRIMARY_IO_BASE 0x1F0U
#define ATA_PRIMARY_CONTROL_BASE 0x3F6U
#define ATA_SECONDARY_IO_BASE 0x170U
#define ATA_SECONDARY_CONTROL_BASE 0x376U

/* Offsets from the I/O base. */
#define ATA_REGISTER_DATA 0U
#define ATA_REGISTER_ERROR 1U
#define ATA_REGISTER_SECTOR_COUNT 2U
#define ATA_REGISTER_LBA_LOW 3U
#define ATA_REGISTER_LBA_MID 4U
#define ATA_REGISTER_LBA_HIGH 5U
#define ATA_REGISTER_DRIVE 6U
#define ATA_REGISTER_STATUS 7U
#define ATA_REGISTER_COMMAND 7U

#define ATA_STATUS_ERROR 0x01U
#define ATA_STATUS_DRQ 0x08U
#define ATA_STATUS_FAULT 0x20U
#define ATA_STATUS_READY 0x40U
#define ATA_STATUS_BUSY 0x80U

#define ATA_COMMAND_READ_SECTORS 0x20U
#define ATA_COMMAND_READ_SECTORS_EXT 0x24U
#define ATA_COMMAND_IDENTIFY 0xECU

#define ATA_DRIVE_SELECT_LBA28 0xE0U
#define ATA_DRIVE_SELECT_LBA48 0x40U
#define ATA_DRIVE_SLAVE 0x10U
#define ATA_DRIVE_LBA28_HIGH_MASK 0x0FU

#define ATA_SECTOR_BYTES 512U
#define ATA_WORDS_PER_SECTOR (ATA_SECTOR_BYTES / 2U)
#define ATA_IDENTIFY_WORDS 256U

/* A sector count of zero means the maximum, which is why these are not the
   masks they look like. */
#define ATA_LBA28_MAX_SECTORS 256U
#define ATA_LBA48_MAX_SECTORS 65536U
#define ATA_LBA28_SECTOR_LIMIT (1ULL << 28)

/* Words of interest in the 256-word IDENTIFY reply. */
#define ATA_IDENTIFY_CAPABILITIES 49U
#define ATA_IDENTIFY_LBA28_SECTORS 60U
#define ATA_IDENTIFY_COMMAND_SETS 83U
#define ATA_IDENTIFY_LBA48_SECTORS 100U

#define ATA_CAPABILITY_LBA 0x0200U
#define ATA_COMMAND_SET_LBA48 0x0400U

/* Bounded rather than "until it is ready": absent hardware reads back as all
   ones forever, and a bootloader that hangs tells the user nothing. */
#define ATA_POLL_ATTEMPTS 1000000U

struct ata_io {
    uint8_t (*read_port_u8)(uint16_t port);
    void (*write_port_u8)(uint16_t port, uint8_t value);
    uint16_t (*read_port_u16)(uint16_t port);
};

struct ata_channel {
    const struct ata_io *io;
    uint16_t io_base;
    uint16_t control_base;
    bool slave;
    bool lba48;
};

/* Identifies the drive and fills in a block device that reads through it.
   False when there is nothing there, or nothing this code can address. */
bool ata_probe(const struct ata_io *io, uint16_t io_base, uint16_t control_base,
               bool slave, struct ata_channel *channel,
               struct block_device *out);

/* The real thing, on x86 ports. Not available to the host tests, which supply
   their own. */
const struct ata_io *ata_port_io(void);

#endif
