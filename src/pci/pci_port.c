#include "cpu/port.h"
#include "pci/pci.h"

uint32_t pci_port_config_read(void *context, uint8_t bus, uint8_t device,
                              uint8_t function, uint8_t offset) {
    (void)context;
    port_write_u32(PCI_CONFIG_ADDRESS_PORT,
                   pci_config_address(bus, device, function, offset));
    return port_read_u32(PCI_CONFIG_DATA_PORT);
}
