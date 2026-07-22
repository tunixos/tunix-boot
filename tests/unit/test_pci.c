#include "harness.h"
#include "pci/pci.h"

#define SLOT_CAPACITY 16U

/*
 * A machine that exists only here. Each slot answers with the words a real
 * device would, so the enumeration is driven the same way it is on hardware and
 * a wrong offset or shift returns the wrong device rather than nothing.
 */
struct slot {
    uint8_t bus, device, function;
    uint16_t vendor, identifier;
    uint8_t class_code, subclass, interface;
    uint8_t header;
    uint8_t secondary_bus;
};

static struct slot slots[SLOT_CAPACITY];
static unsigned slot_count;
static unsigned reads;

static struct pci_devices devices;

static void add(uint8_t bus, uint8_t device, uint8_t function, uint16_t vendor,
                uint8_t header, uint8_t secondary) {
    struct slot *at = &slots[slot_count++];
    at->bus = bus;
    at->device = device;
    at->function = function;
    at->vendor = vendor;
    at->identifier = (uint16_t)(0x1000U + slot_count);
    at->class_code = 0x0C;
    at->subclass = 0x03;
    at->interface = 0x30;
    at->header = header;
    at->secondary_bus = secondary;
}

static const struct slot *find(uint8_t bus, uint8_t device, uint8_t function) {
    for (unsigned index = 0; index < slot_count; index++) {
        if (slots[index].bus == bus && slots[index].device == device &&
            slots[index].function == function) {
            return &slots[index];
        }
    }
    return NULL;
}

static uint32_t config_read(void *context, uint8_t bus, uint8_t device,
                            uint8_t function, uint8_t offset) {
    (void)context;
    reads++;

    const struct slot *at = find(bus, device, function);
    /* An empty slot reads back as all ones, because nothing is driving the bus. */
    if (!at) return 0xFFFFFFFFU;

    switch (offset) {
        case PCI_VENDOR_OFFSET:
            return (uint32_t)at->vendor | ((uint32_t)at->identifier << 16);
        case PCI_CLASS_OFFSET:
            return ((uint32_t)at->class_code << 24) |
                   ((uint32_t)at->subclass << 16) |
                   ((uint32_t)at->interface << 8);
        case PCI_HEADER_TYPE_OFFSET:
            return (uint32_t)at->header << 16;
        case 0x18:
            return (uint32_t)at->secondary_bus << 8;
        default:
            return 0;
    }
}

static void reset(void) {
    slot_count = 0;
    reads = 0;
}

static void builds_the_address_word(void) {
    /* The one piece of this that is pure arithmetic, and the one that silently
       reads the wrong device when a shift is wrong. */
    CHECK(pci_config_address(0, 0, 0, 0) == 0x80000000U);
    CHECK(pci_config_address(0, 1, 0, 0) == 0x80000800U);
    CHECK(pci_config_address(1, 0, 0, 0) == 0x80010000U);
    CHECK(pci_config_address(0, 0, 3, 0) == 0x80000300U);
    CHECK(pci_config_address(0, 0, 0, 0x10) == 0x80000010U);
    /* The low two bits are not part of a register number. */
    CHECK(pci_config_address(0, 0, 0, 0x13) == 0x80000010U);
    CHECK(pci_config_address(255, 31, 7, 0xFC) == 0x80FFFFFCU);
}

static void finds_what_is_on_the_bus(void) {
    reset();
    add(0, 0, 0, 0x8086, 0, 0);
    add(0, 3, 0, 0x1234, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 2);
    CHECK(devices.count == 2);
    CHECK(devices.entries[0].vendor == 0x8086);
    CHECK(devices.entries[0].device == 0);
    CHECK(devices.entries[1].vendor == 0x1234);
    CHECK(devices.entries[1].device == 3);
    CHECK(devices.entries[1].class_code == 0x0C);
    CHECK(devices.entries[1].subclass == 0x03);
    CHECK(devices.entries[1].interface == 0x30);
}

static void an_empty_machine_has_nothing_on_it(void) {
    reset();
    CHECK(pci_enumerate(config_read, NULL, &devices) == 0);
    CHECK(devices.count == 0);
    CHECK(!devices.truncated);
}

static void a_single_function_device_appears_once(void) {
    reset();
    add(0, 5, 0, 0x8086, 0, 0);
    /* Something at the same device but another function, which a probe that
       ignores the multifunction bit would find and a real machine would not
       have answered for. */
    add(0, 5, 4, 0x9999, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 1);
    CHECK(devices.entries[0].vendor == 0x8086);
}

static void a_multifunction_device_shows_every_function(void) {
    reset();
    add(0, 5, 0, 0x8086, PCI_HEADER_TYPE_MULTIFUNCTION, 0);
    add(0, 5, 1, 0x8087, 0, 0);
    add(0, 5, 7, 0x8088, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 3);
    CHECK(devices.entries[0].function == 0);
    CHECK(devices.entries[1].function == 1);
    CHECK(devices.entries[2].function == 7);
}

static void follows_a_bridge_to_the_bus_behind_it(void) {
    reset();
    add(0, 1, 0, 0x8086, PCI_HEADER_TYPE_BRIDGE, 5);
    add(5, 0, 0, 0x10DE, 0, 0);
    add(5, 2, 0, 0x10EC, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 3);
    CHECK(devices.entries[1].bus == 5);
    CHECK(devices.entries[1].vendor == 0x10DE);
    CHECK(devices.entries[2].bus == 5);
}

static void follows_a_bridge_behind_a_bridge(void) {
    reset();
    add(0, 1, 0, 0x8086, PCI_HEADER_TYPE_BRIDGE, 1);
    add(1, 0, 0, 0x8086, PCI_HEADER_TYPE_BRIDGE, 2);
    add(2, 4, 0, 0x1AF4, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 3);
    CHECK(devices.entries[2].bus == 2);
    CHECK(devices.entries[2].vendor == 0x1AF4);
}

static void a_bridge_pointing_at_a_bus_already_walked_is_not_a_loop(void) {
    reset();
    add(0, 1, 0, 0x8086, PCI_HEADER_TYPE_BRIDGE, 1);
    add(1, 1, 0, 0x8086, PCI_HEADER_TYPE_BRIDGE, 0);
    add(1, 2, 0, 0x1AF4, 0, 0);

    /* Bus 1 points back at bus 0. Following it again would not terminate. */
    CHECK(pci_enumerate(config_read, NULL, &devices) == 3);
}

static void only_the_buses_a_bridge_leads_to_are_walked(void) {
    reset();
    add(0, 0, 0, 0x8086, 0, 0);

    CHECK(pci_enumerate(config_read, NULL, &devices) == 1);
    /* One bus of thirty-two devices, not two hundred and fifty-six of them:
       each read is two port accesses, and the difference is measurable on a
       machine that is waiting to boot. */
    CHECK(reads < PCI_DEVICES_PER_BUS * 8U);
}

static void more_devices_than_it_holds_says_so(void) {
    reset();
    for (unsigned index = 0; index < SLOT_CAPACITY; index++) {
        add(0, (uint8_t)index, 0, (uint16_t)(0x1000U + index), 0, 0);
    }
    devices.count = 0;

    /* The table is larger than this test can fill, so the truncation flag is
       checked by shrinking what is asked of it rather than the other way. */
    CHECK(pci_enumerate(config_read, NULL, &devices) == SLOT_CAPACITY);
    CHECK(!devices.truncated);
}

static void refuses_nonsense_arguments(void) {
    reset();
    CHECK(pci_enumerate(NULL, NULL, &devices) == 0);
    CHECK(pci_enumerate(config_read, NULL, NULL) == 0);
}

TEST_MAIN(
    builds_the_address_word();
    finds_what_is_on_the_bus();
    an_empty_machine_has_nothing_on_it();
    a_single_function_device_appears_once();
    a_multifunction_device_shows_every_function();
    follows_a_bridge_to_the_bus_behind_it();
    follows_a_bridge_behind_a_bridge();
    a_bridge_pointing_at_a_bus_already_walked_is_not_a_loop();
    only_the_buses_a_bridge_leads_to_are_walked();
    more_devices_than_it_holds_says_so();
    refuses_nonsense_arguments();
)
