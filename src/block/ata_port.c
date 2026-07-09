#include "block/ata.h"
#include "cpu/port.h"

static const struct ata_io port_io = {
    .read_port_u8 = port_read_u8,
    .write_port_u8 = port_write_u8,
    .read_port_u16 = port_read_u16,
};

const struct ata_io *ata_port_io(void) {
    return &port_io;
}
