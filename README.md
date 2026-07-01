# tunix-boot

A modular x86_64 bootloader for BIOS and UEFI.

The goal is not a teaching example. It is a bootloader whose core is firmware
agnostic, whose parsers never trust the disk, and whose logic can be unit tested
on the build host rather than only in a virtual machine.

## Layout

    src/util        bounds-checked reading, strings, checked arithmetic
    src/cpu         cpuid, msr, gdt, paging, feature detection
    src/memory      memory map, arena and page allocation
    src/fw          firmware abstraction; bios/ and uefi/ backends
    src/block       block device interface and drivers
    src/fs          filesystem interface, fat32, ext2
    src/elf         elf64 parsing, loading, relocation
    src/config      configuration lexer and parser
    src/video       framebuffer abstraction
    src/terminal    framebuffer terminal
    src/acpi        rsdp, xsdt, madt, fadt, mcfg
    src/smp         application processor startup
    src/pci         enumeration
    src/protocol    the tunix boot protocol and kernel handoff
    src/boot        orchestration
    include/tunix-boot   the protocol header a kernel includes

Layers depend downwards only. `tools/check-layering.py` enforces that in CI, so
the architecture lives in the build rather than in a document.

## Building

    meson setup build --cross-file meson/cross/x86_64-bios.txt
    meson compile -C build

Host unit tests need no cross compiler:

    meson setup build-host -Dtests=true
    meson test -C build-host

## Documentation

`docs/architecture.md` covers the layering, the firmware abstraction and the
initialisation order. `docs/protocol.md` is the boot protocol ABI.
