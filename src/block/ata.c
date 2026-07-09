#include "block/ata.h"
#include "util/checked.h"

#define ATA_NAME "ata"
#define BITS_PER_BYTE 8U

static uint8_t status_of(const struct ata_channel *channel) {
    return channel->io->read_port_u8(
        (uint16_t)(channel->io_base + ATA_REGISTER_STATUS));
}

static void write_register(const struct ata_channel *channel, uint16_t offset,
                           uint8_t value) {
    channel->io->write_port_u8((uint16_t)(channel->io_base + offset), value);
}

/* The status register needs time to settle after a command; reading the
   alternate status four times is the documented way to spend it. */
static void settle(const struct ata_channel *channel) {
    for (unsigned index = 0; index < 4; index++) {
        (void)channel->io->read_port_u8(channel->control_base);
    }
}

static bool wait_while_busy(const struct ata_channel *channel, uint8_t *out) {
    for (unsigned attempt = 0; attempt < ATA_POLL_ATTEMPTS; attempt++) {
        uint8_t status = status_of(channel);
        /* All ones is a floating bus: there is no drive to wait for. */
        if (status == 0xFFU) return false;
        if ((status & ATA_STATUS_BUSY) == 0) {
            *out = status;
            return true;
        }
    }
    return false;
}

static bool wait_for_data(const struct ata_channel *channel) {
    uint8_t status;
    if (!wait_while_busy(channel, &status)) return false;
    if (status & (ATA_STATUS_ERROR | ATA_STATUS_FAULT)) return false;
    return (status & ATA_STATUS_DRQ) != 0;
}

static void select_drive(const struct ata_channel *channel, uint8_t extra) {
    uint8_t value = (uint8_t)(extra | (channel->slave ? ATA_DRIVE_SLAVE : 0U));
    write_register(channel, ATA_REGISTER_DRIVE, value);
    settle(channel);
}

static void read_sector_words(const struct ata_channel *channel, uint8_t *out) {
    uint16_t port = (uint16_t)(channel->io_base + ATA_REGISTER_DATA);
    for (unsigned index = 0; index < ATA_WORDS_PER_SECTOR; index++) {
        uint16_t word = channel->io->read_port_u16(port);
        out[index * 2U] = (uint8_t)(word & 0xFFU);
        out[index * 2U + 1U] = (uint8_t)(word >> BITS_PER_BYTE);
    }
}

static bool identify(const struct ata_channel *channel, uint16_t *words) {
    select_drive(channel, ATA_DRIVE_SELECT_LBA28);

    /* Zeroed, so a drive that answers without having been asked about an
       address cannot be mistaken for one reporting a size. */
    write_register(channel, ATA_REGISTER_SECTOR_COUNT, 0);
    write_register(channel, ATA_REGISTER_LBA_LOW, 0);
    write_register(channel, ATA_REGISTER_LBA_MID, 0);
    write_register(channel, ATA_REGISTER_LBA_HIGH, 0);
    write_register(channel, ATA_REGISTER_COMMAND, ATA_COMMAND_IDENTIFY);
    settle(channel);

    if (status_of(channel) == 0) return false;

    /* A non-zero cylinder pair here is a device that speaks a different
       protocol answering an ATA command. It is not a disk we can read. */
    uint8_t mid = channel->io->read_port_u8(
        (uint16_t)(channel->io_base + ATA_REGISTER_LBA_MID));
    uint8_t high = channel->io->read_port_u8(
        (uint16_t)(channel->io_base + ATA_REGISTER_LBA_HIGH));
    if (mid != 0 || high != 0) return false;

    if (!wait_for_data(channel)) return false;

    uint16_t port = (uint16_t)(channel->io_base + ATA_REGISTER_DATA);
    for (unsigned index = 0; index < ATA_IDENTIFY_WORDS; index++) {
        words[index] = channel->io->read_port_u16(port);
    }
    return true;
}

static void issue_lba28(const struct ata_channel *channel, uint64_t lba,
                        uint32_t count) {
    uint8_t high = (uint8_t)((lba >> 24) & ATA_DRIVE_LBA28_HIGH_MASK);
    select_drive(channel, (uint8_t)(ATA_DRIVE_SELECT_LBA28 | high));

    write_register(channel, ATA_REGISTER_SECTOR_COUNT,
                   (uint8_t)(count == ATA_LBA28_MAX_SECTORS ? 0U : count));
    write_register(channel, ATA_REGISTER_LBA_LOW, (uint8_t)(lba & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_MID, (uint8_t)((lba >> 8) & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFFU));
    write_register(channel, ATA_REGISTER_COMMAND, ATA_COMMAND_READ_SECTORS);
}

static void issue_lba48(const struct ata_channel *channel, uint64_t lba,
                        uint32_t count) {
    select_drive(channel, ATA_DRIVE_SELECT_LBA48);

    /* The high half goes first: each of these registers is a two-deep queue,
       and the second write pushes the first back. */
    write_register(channel, ATA_REGISTER_SECTOR_COUNT, (uint8_t)((count >> 8) & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_LOW, (uint8_t)((lba >> 24) & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_MID, (uint8_t)((lba >> 32) & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_HIGH, (uint8_t)((lba >> 40) & 0xFFU));

    write_register(channel, ATA_REGISTER_SECTOR_COUNT, (uint8_t)(count & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_LOW, (uint8_t)(lba & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_MID, (uint8_t)((lba >> 8) & 0xFFU));
    write_register(channel, ATA_REGISTER_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFFU));

    write_register(channel, ATA_REGISTER_COMMAND, ATA_COMMAND_READ_SECTORS_EXT);
}

static bool ata_read_sectors(const struct block_device *device, uint64_t lba,
                             uint32_t count, void *out) {
    const struct ata_channel *channel = (const struct ata_channel *)device->context;
    if (!channel || count == 0) return false;

    uint64_t end;
    if (!checked_add_u64(lba, count, &end)) return false;

    if (channel->lba48) {
        if (count > ATA_LBA48_MAX_SECTORS) return false;
        issue_lba48(channel, lba, count);
    } else {
        if (end > ATA_LBA28_SECTOR_LIMIT) return false;
        if (count > ATA_LBA28_MAX_SECTORS) return false;
        issue_lba28(channel, lba, count);
    }
    settle(channel);

    /* Each sector is announced separately, so the wait is per sector and not
       once for the whole transfer. */
    uint8_t *cursor = (uint8_t *)out;
    for (uint32_t index = 0; index < count; index++) {
        if (!wait_for_data(channel)) return false;
        read_sector_words(channel, cursor);
        cursor += ATA_SECTOR_BYTES;
    }
    return true;
}

static uint64_t sectors_from_identify(const uint16_t *words, bool lba48) {
    if (lba48) {
        uint64_t total = 0;
        for (unsigned index = 0; index < 4; index++) {
            total |= (uint64_t)words[ATA_IDENTIFY_LBA48_SECTORS + index]
                     << (index * 16U);
        }
        if (total != 0) return total;
    }
    return (uint64_t)words[ATA_IDENTIFY_LBA28_SECTORS] |
           ((uint64_t)words[ATA_IDENTIFY_LBA28_SECTORS + 1U] << 16U);
}

bool ata_probe(const struct ata_io *io, uint16_t io_base, uint16_t control_base,
               bool slave, struct ata_channel *channel,
               struct block_device *out) {
    if (!io || !io->read_port_u8 || !io->write_port_u8 || !io->read_port_u16)
        return false;

    channel->io = io;
    channel->io_base = io_base;
    channel->control_base = control_base;
    channel->slave = slave;
    channel->lba48 = false;

    uint16_t words[ATA_IDENTIFY_WORDS];
    if (!identify(channel, words)) return false;

    /* CHS-only drives exist and this loader does not address them. Refusing is
       better than reading the wrong sectors. */
    if ((words[ATA_IDENTIFY_CAPABILITIES] & ATA_CAPABILITY_LBA) == 0) return false;

    channel->lba48 =
        (words[ATA_IDENTIFY_COMMAND_SETS] & ATA_COMMAND_SET_LBA48) != 0;

    uint64_t sectors = sectors_from_identify(words, channel->lba48);
    if (sectors == 0) return false;

    out->name = ATA_NAME;
    out->read_sectors = ata_read_sectors;
    out->sector_bytes = ATA_SECTOR_BYTES;
    out->sector_count = sectors;
    out->context = channel;
    return true;
}
