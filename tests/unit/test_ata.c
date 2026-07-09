#include "harness.h"
#include "block/ata.h"
#include "util/mem.h"

#define DRIVE_SECTORS 2048U
#define DRIVE_BYTES (DRIVE_SECTORS * ATA_SECTOR_BYTES)

/*
 * A drive that exists only here: it holds the register writes, works out what
 * was asked for when a command lands, and then feeds the data register a sector
 * at a time exactly as hardware does. If the driver sets a register wrongly,
 * this returns the wrong sector rather than quietly working.
 */
struct simulated_drive {
    uint8_t storage[DRIVE_BYTES];
    uint8_t registers[8];
    /* Two-deep, because LBA48 writes each address register twice. */
    uint8_t previous[8];

    bool present;
    bool supports_lba;
    bool supports_lba48;
    bool signature_mismatch;
    bool fail_next_command;

    uint16_t pending[ATA_IDENTIFY_WORDS];
    unsigned pending_words;
    unsigned pending_read;

    uint64_t served_lba;
    uint32_t served_count;
    uint8_t status;
};

static struct simulated_drive drive;

static void queue_words(const uint16_t *words, unsigned count) {
    for (unsigned index = 0; index < count; index++) drive.pending[index] = words[index];
    drive.pending_words = count;
    drive.pending_read = 0;
    drive.status = ATA_STATUS_READY | ATA_STATUS_DRQ;
}

static void build_identify(void) {
    uint16_t words[ATA_IDENTIFY_WORDS];
    for (unsigned index = 0; index < ATA_IDENTIFY_WORDS; index++) words[index] = 0;

    if (drive.supports_lba) words[ATA_IDENTIFY_CAPABILITIES] = ATA_CAPABILITY_LBA;
    if (drive.supports_lba48) {
        words[ATA_IDENTIFY_COMMAND_SETS] = ATA_COMMAND_SET_LBA48;
        words[ATA_IDENTIFY_LBA48_SECTORS] = (uint16_t)(DRIVE_SECTORS & 0xFFFFU);
    }
    words[ATA_IDENTIFY_LBA28_SECTORS] = (uint16_t)(DRIVE_SECTORS & 0xFFFFU);
    queue_words(words, ATA_IDENTIFY_WORDS);
}

static uint64_t decode_lba(void) {
    const uint8_t *now = drive.registers;
    if (drive.supports_lba48) {
        const uint8_t *before = drive.previous;
        return (uint64_t)now[ATA_REGISTER_LBA_LOW] |
               ((uint64_t)now[ATA_REGISTER_LBA_MID] << 8) |
               ((uint64_t)now[ATA_REGISTER_LBA_HIGH] << 16) |
               ((uint64_t)before[ATA_REGISTER_LBA_LOW] << 24) |
               ((uint64_t)before[ATA_REGISTER_LBA_MID] << 32) |
               ((uint64_t)before[ATA_REGISTER_LBA_HIGH] << 40);
    }
    return (uint64_t)now[ATA_REGISTER_LBA_LOW] |
           ((uint64_t)now[ATA_REGISTER_LBA_MID] << 8) |
           ((uint64_t)now[ATA_REGISTER_LBA_HIGH] << 16) |
           ((uint64_t)(now[ATA_REGISTER_DRIVE] & ATA_DRIVE_LBA28_HIGH_MASK) << 24);
}

static uint32_t decode_count(void) {
    uint32_t low = drive.registers[ATA_REGISTER_SECTOR_COUNT];
    if (drive.supports_lba48) {
        uint32_t high = drive.previous[ATA_REGISTER_SECTOR_COUNT];
        uint32_t count = (high << 8) | low;
        return count == 0 ? ATA_LBA48_MAX_SECTORS : count;
    }
    return low == 0 ? ATA_LBA28_MAX_SECTORS : low;
}

static void begin_read(void) {
    drive.served_lba = decode_lba();
    drive.served_count = decode_count();

    if (drive.fail_next_command ||
        drive.served_lba + drive.served_count > DRIVE_SECTORS) {
        drive.status = ATA_STATUS_READY | ATA_STATUS_ERROR;
        drive.pending_words = 0;
        return;
    }
    drive.status = ATA_STATUS_READY | ATA_STATUS_DRQ;
    drive.pending_read = 0;
    drive.pending_words = 0;
}

static uint16_t next_data_word(void) {
    if (drive.pending_words > 0) {
        if (drive.pending_read >= drive.pending_words) return 0;
        return drive.pending[drive.pending_read++];
    }
    /* Sector data, streamed straight out of storage. */
    uint64_t byte = drive.served_lba * ATA_SECTOR_BYTES + drive.pending_read * 2U;
    drive.pending_read++;
    if (byte + 1 >= DRIVE_BYTES) return 0;
    return (uint16_t)(drive.storage[byte] | (drive.storage[byte + 1] << 8));
}

static uint8_t simulated_read_u8(uint16_t port) {
    if (!drive.present) return 0xFFU;
    if (port == ATA_PRIMARY_CONTROL_BASE) return drive.status;

    unsigned offset = (unsigned)(port - ATA_PRIMARY_IO_BASE);
    if (offset == ATA_REGISTER_STATUS) return drive.status;
    if (drive.signature_mismatch &&
        (offset == ATA_REGISTER_LBA_MID || offset == ATA_REGISTER_LBA_HIGH)) {
        return 0xEBU;
    }
    return drive.registers[offset];
}

static void simulated_write_u8(uint16_t port, uint8_t value) {
    unsigned offset = (unsigned)(port - ATA_PRIMARY_IO_BASE);
    if (offset == ATA_REGISTER_COMMAND) {
        if (value == ATA_COMMAND_IDENTIFY) {
            build_identify();
        } else if (value == ATA_COMMAND_READ_SECTORS ||
                   value == ATA_COMMAND_READ_SECTORS_EXT) {
            begin_read();
        }
        return;
    }
    drive.previous[offset] = drive.registers[offset];
    drive.registers[offset] = value;
}

static uint16_t simulated_read_u16(uint16_t port) {
    (void)port;
    return next_data_word();
}

static const struct ata_io simulated_io = {
    .read_port_u8 = simulated_read_u8,
    .write_port_u8 = simulated_write_u8,
    .read_port_u16 = simulated_read_u16,
};

static uint8_t pattern(size_t index) {
    return (uint8_t)(index * 61U + (index >> 9) * 13U + 3U);
}

static struct ata_channel channel;
static struct block_device device;

static void reset_drive(bool lba48) {
    for (size_t index = 0; index < DRIVE_BYTES; index++)
        drive.storage[index] = pattern(index);
    for (unsigned index = 0; index < 8; index++) {
        drive.registers[index] = 0;
        drive.previous[index] = 0;
    }
    drive.present = true;
    drive.supports_lba = true;
    drive.supports_lba48 = lba48;
    drive.signature_mismatch = false;
    drive.fail_next_command = false;
    drive.pending_words = 0;
    drive.pending_read = 0;
    drive.status = ATA_STATUS_READY;
}

static bool probe(void) {
    return ata_probe(&simulated_io, ATA_PRIMARY_IO_BASE, ATA_PRIMARY_CONTROL_BASE,
                     false, &channel, &device);
}

static bool matches(const uint8_t *got, size_t offset, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (got[index] != pattern(offset + index)) return false;
    }
    return true;
}

static void identifies_a_drive_and_its_size(void) {
    reset_drive(true);
    CHECK(probe());
    CHECK(device.sector_bytes == ATA_SECTOR_BYTES);
    CHECK(device.sector_count == DRIVE_SECTORS);
    CHECK(channel.lba48);
    CHECK(block_device_valid(&device));
}

static void an_absent_drive_is_not_invented(void) {
    reset_drive(true);
    drive.present = false;
    /* A floating bus reads back as all ones; waiting for it to go ready is how
       a loader hangs on a machine with one disk instead of two. */
    CHECK(!probe());
}

static void a_device_answering_with_another_protocol_is_refused(void) {
    reset_drive(true);
    drive.signature_mismatch = true;
    CHECK(!probe());
}

static void a_drive_without_lba_is_refused(void) {
    reset_drive(false);
    drive.supports_lba = false;
    CHECK(!probe());
}

static void reads_the_sector_that_was_asked_for(void) {
    static uint8_t got[4 * ATA_SECTOR_BYTES];
    reset_drive(true);
    CHECK(probe());

    CHECK(block_read(&device, 9, 4, got));
    CHECK(drive.served_lba == 9);
    CHECK(drive.served_count == 4);
    CHECK(matches(got, 9 * ATA_SECTOR_BYTES, sizeof got));
}

/* The registers are written in a different order and width for LBA48, and the
   address is the thing that silently comes out wrong when that is muddled. */
static void addresses_a_high_sector_under_lba48(void) {
    static uint8_t got[ATA_SECTOR_BYTES];
    reset_drive(true);
    CHECK(probe());

    CHECK(!block_read(&device, DRIVE_SECTORS, 1, got));
    /* Refused above the reported size, so reach the last sector instead. */
    CHECK(block_read(&device, DRIVE_SECTORS - 1, 1, got));
    CHECK(drive.served_lba == DRIVE_SECTORS - 1);
    CHECK(matches(got, (DRIVE_SECTORS - 1) * ATA_SECTOR_BYTES, sizeof got));
}

static void addresses_a_sector_under_lba28(void) {
    static uint8_t got[2 * ATA_SECTOR_BYTES];
    reset_drive(false);
    CHECK(probe());
    CHECK(!channel.lba48);

    CHECK(block_read(&device, 1000, 2, got));
    CHECK(drive.served_lba == 1000);
    CHECK(drive.served_count == 2);
    CHECK(matches(got, 1000 * ATA_SECTOR_BYTES, sizeof got));
}

static void an_error_from_the_drive_is_a_failed_read(void) {
    static uint8_t got[ATA_SECTOR_BYTES];
    reset_drive(true);
    CHECK(probe());

    drive.fail_next_command = true;
    CHECK(!block_read(&device, 0, 1, got));
}

static void byte_ranges_work_through_the_driver(void) {
    static uint8_t got[ATA_SECTOR_BYTES + 64];
    reset_drive(true);
    CHECK(probe());

    const uint64_t offset = 3 * ATA_SECTOR_BYTES + 17;
    CHECK(block_read_bytes(&device, offset, sizeof got, got));
    CHECK(matches(got, offset, sizeof got));
}

TEST_MAIN(
    identifies_a_drive_and_its_size();
    an_absent_drive_is_not_invented();
    a_device_answering_with_another_protocol_is_refused();
    a_drive_without_lba_is_refused();
    reads_the_sector_that_was_asked_for();
    addresses_a_high_sector_under_lba48();
    addresses_a_sector_under_lba28();
    an_error_from_the_drive_is_a_failed_read();
    byte_ranges_work_through_the_driver();
)
