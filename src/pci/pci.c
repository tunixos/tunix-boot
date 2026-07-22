#include "pci/pci.h"

#define BUS_SHIFT 16U
#define DEVICE_SHIFT 11U
#define FUNCTION_SHIFT 8U
#define OFFSET_MASK 0xFCU

#define BRIDGE_SECONDARY_BUS_OFFSET 0x18U
#define BRIDGE_SECONDARY_BUS_SHIFT 8U

uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function,
                            uint8_t offset) {
    return PCI_CONFIG_ENABLE | ((uint32_t)bus << BUS_SHIFT) |
           ((uint32_t)device << DEVICE_SHIFT) |
           ((uint32_t)function << FUNCTION_SHIFT) | (offset & OFFSET_MASK);
}

struct walk {
    pci_config_read_fn read;
    void *context;
    struct pci_devices *out;
    /* Which buses are still to be walked, and which have been. A bridge that
       points back at a bus already seen would otherwise be a loop. */
    bool pending[PCI_BUSES];
    bool seen[PCI_BUSES];
};

static bool record(struct walk *walk, uint8_t bus, uint8_t device,
                   uint8_t function, uint32_t identity, uint32_t class_word) {
    if (walk->out->count == PCI_MAX_DEVICES) {
        walk->out->truncated = true;
        return false;
    }

    struct pci_device *entry = &walk->out->entries[walk->out->count++];
    entry->bus = bus;
    entry->device = device;
    entry->function = function;
    entry->vendor = (uint16_t)(identity & 0xFFFFU);
    entry->identifier = (uint16_t)(identity >> 16);
    entry->interface = (uint8_t)((class_word >> 8) & 0xFFU);
    entry->subclass = (uint8_t)((class_word >> 16) & 0xFFU);
    entry->class_code = (uint8_t)((class_word >> 24) & 0xFFU);
    return true;
}

static void visit_function(struct walk *walk, uint8_t bus, uint8_t device,
                           uint8_t function) {
    uint32_t identity =
        walk->read(walk->context, bus, device, function, PCI_VENDOR_OFFSET);
    if ((identity & 0xFFFFU) == PCI_VENDOR_NONE) return;

    uint32_t class_word =
        walk->read(walk->context, bus, device, function, PCI_CLASS_OFFSET);
    if (!record(walk, bus, device, function, identity, class_word)) return;

    uint32_t header_word =
        walk->read(walk->context, bus, device, function, PCI_HEADER_TYPE_OFFSET);
    uint8_t header = (uint8_t)((header_word >> 16) & 0xFFU);

    if ((header & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE) return;

    /* A bridge names the bus behind it, and that bus is where the rest of the
       machine is. Walking only these is what keeps this to a few hundred reads
       instead of sixty-five thousand. */
    uint32_t bus_word = walk->read(walk->context, bus, device, function,
                                   BRIDGE_SECONDARY_BUS_OFFSET);
    uint8_t secondary = (uint8_t)((bus_word >> BRIDGE_SECONDARY_BUS_SHIFT) & 0xFFU);
    if (!walk->seen[secondary]) walk->pending[secondary] = true;
}

static void visit_device(struct walk *walk, uint8_t bus, uint8_t device) {
    uint32_t identity = walk->read(walk->context, bus, device, 0,
                                   PCI_VENDOR_OFFSET);
    if ((identity & 0xFFFFU) == PCI_VENDOR_NONE) return;

    visit_function(walk, bus, device, 0);

    uint32_t header_word =
        walk->read(walk->context, bus, device, 0, PCI_HEADER_TYPE_OFFSET);
    uint8_t header = (uint8_t)((header_word >> 16) & 0xFFU);

    /* Only a device that says so has more than one function. Probing all eight
       regardless is how a single-function device appears eight times. */
    if ((header & PCI_HEADER_TYPE_MULTIFUNCTION) == 0) return;

    for (uint8_t function = 1; function < PCI_FUNCTIONS_PER_DEVICE; function++) {
        visit_function(walk, bus, device, function);
    }
}

unsigned pci_enumerate(pci_config_read_fn read, void *context,
                       struct pci_devices *out) {
    if (!read || !out) return 0;

    out->count = 0;
    out->truncated = false;

    static struct walk walk;
    walk.read = read;
    walk.context = context;
    walk.out = out;
    for (unsigned index = 0; index < PCI_BUSES; index++) {
        walk.pending[index] = false;
        walk.seen[index] = false;
    }
    walk.pending[0] = true;

    for (;;) {
        unsigned bus = PCI_BUSES;
        for (unsigned index = 0; index < PCI_BUSES; index++) {
            if (walk.pending[index]) {
                bus = index;
                break;
            }
        }
        if (bus == PCI_BUSES) break;

        walk.pending[bus] = false;
        walk.seen[bus] = true;

        for (uint8_t device = 0; device < PCI_DEVICES_PER_BUS; device++) {
            visit_device(&walk, (uint8_t)bus, device);
        }
    }
    return out->count;
}
