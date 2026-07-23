# tunix-boot

A modular x86_64 bootloader for BIOS and UEFI.

The goal is not a teaching example. It is a bootloader whose core is firmware
agnostic, whose parsers never trust the disk, and whose logic can be unit tested
on the build host rather than only in a virtual machine.

Both firmware paths run the same core. Under BIOS a boot record loads a second
stage that brings the machine to long mode; under UEFI the firmware loads a
PE application directly. Everything above `struct fw_ops` — the disk driver, the
filesystems, the ELF loader, paging, the boot protocol — is one body of code
that does not know which of the two got it there.

## Layout

    src/util        bounds-checked reading, strings, checked arithmetic
    src/cpu         cpuid, gdt, control registers, port i/o
    src/memory      memory map, bump arena, page tables
    src/fw          firmware abstraction; bios/ and uefi/ backends
    src/block       block devices, ata pio, mbr and gpt partitions
    src/fs          fat32 and ext2, read only
    src/elf         elf64 parsing and loading
    src/config      the configuration file
    src/video       framebuffer, whatever layout firmware reports
    src/terminal    a terminal on it, and the font
    src/acpi        rsdp, rsdt/xsdt, madt
    src/pci         bus enumeration
    src/protocol    the boot protocol and kernel handoff
    src/boot        orchestration

Layers depend downwards only. `tools/check-layering.py` runs as a test, so the
architecture lives in the build rather than in a document.

## Building

BIOS, producing a bootable disk image:

    meson setup build-bios --cross-file meson/cross/x86_64-bios.txt
    meson compile -C build-bios

UEFI, producing `BOOTX64.EFI`. Needs clang and lld; gcc cannot emit PE:

    meson setup build-uefi --cross-file meson/cross/x86_64-uefi.txt \
        -Dfirmware=uefi
    meson compile -C build-uefi

Host unit tests need no cross compiler and take under a second:

    meson setup build-host -Dtests=true
    meson test -C build-host

`meson test` on either firmware build also boots it under QEMU and reads the
serial line, so the whole chain down to the kernel printing is one test. The
UEFI test needs OVMF.

## What it does not do

- Start application processors. The MADT is parsed and the processor list is
  handed to the kernel, but INIT-SIPI-SIPI and a trampoline are not written.
- AHCI or NVMe. The disk is driven over ATA PIO.
- Secure Boot, or verifying anything it loads.
- Write to a disk, ever.

## Documentation

`docs/architecture.md` covers the layering, the firmware abstraction and the
initialisation order. `docs/protocol.md` is the boot protocol ABI.
