#ifndef TUNIX_BOOT_PCI_PCI_H
#define TUNIX_BOOT_PCI_PCI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What is plugged into the machine.
 *
 * The loader does not drive any of it — the one device it uses, the disk, it
 * reaches through fixed ports. This exists so the kernel is handed the list
 * rather than having to walk the buses again before it has a driver for
 * anything, and so a machine that will not boot can say what is in it.
 *
 * Reading a configuration word is the whole hardware surface, so that is what
 * is injected: the tests drive the enumeration against a machine that exists
 * only in the test.
 */

#define PCI_CONFIG_ADDRESS_PORT 0xCF8U
#define PCI_CONFIG_DATA_PORT 0xCFCU
/* Bit 31 of the address word is what makes the access a configuration cycle
   rather than nothing at all. */
#define PCI_CONFIG_ENABLE 0x80000000U

#define PCI_VENDOR_OFFSET 0x00U
#define PCI_DEVICE_OFFSET 0x02U
#define PCI_CLASS_OFFSET 0x08U
#define PCI_HEADER_TYPE_OFFSET 0x0CU

#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80U
#define PCI_HEADER_TYPE_MASK 0x7FU
#define PCI_HEADER_TYPE_BRIDGE 0x01U

/* A slot that answers with all ones is a slot with nothing in it: the bus is
   not driven, and the pull-ups are what the processor reads back. */
#define PCI_VENDOR_NONE 0xFFFFU

#define PCI_BUSES 256U
#define PCI_DEVICES_PER_BUS 32U
#define PCI_FUNCTIONS_PER_DEVICE 8U

#define PCI_MAX_DEVICES 64U

/* Reads the 32-bit configuration word at `offset`, which is always a multiple
   of four. Everything above is arithmetic on what this returns. */
typedef uint32_t (*pci_config_read_fn)(void *context, uint8_t bus,
                                       uint8_t device, uint8_t function,
                                       uint8_t offset);

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor;
    uint16_t identifier;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t interface;
};

struct pci_devices {
    struct pci_device entries[PCI_MAX_DEVICES];
    unsigned count;
    bool truncated;
};

/* Walks the buses and records what answers. Returns how many were found.

   Only the buses a bridge leads to are walked, rather than all 256: a machine
   with one bus would otherwise cost 65536 configuration reads to find that
   out, and each one is two port accesses. */
unsigned pci_enumerate(pci_config_read_fn read, void *context,
                       struct pci_devices *out);

/* The address word that selects a configuration register. Separate because it
   is the one piece of this that is pure arithmetic and easy to get wrong. */
uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function,
                            uint8_t offset);

/* The real thing, on x86 ports. Not available to the host tests. */
uint32_t pci_port_config_read(void *context, uint8_t bus, uint8_t device,
                              uint8_t function, uint8_t offset);

#endif
