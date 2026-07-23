#!/bin/sh
# Boot the UEFI application under OVMF and require the same lines from the core
# that the BIOS test does. Everything above struct fw_ops is the same code, so
# what this really checks is that the firmware backend under it agrees.
set -eu

application="$1"
data_image="$2"
ovmf_code="$3"
ovmf_vars="$4"
memory_mib=2048
timeout_seconds=60

# The firmware writes to its variable store, so it needs a copy it may modify.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$ovmf_vars" "$work/vars.fd"
chmod u+w "$work/vars.fd"

# QEMU builds a FAT filesystem out of a directory, which saves making an image
# just to hold one file. BOOTX64.EFI is the path the firmware looks for on its
# own. That filesystem is FAT16, which is why what the loader itself reads is a
# separate disk rather than this one.
mkdir -p "$work/esp/EFI/BOOT"
cp "$application" "$work/esp/EFI/BOOT/BOOTX64.EFI"

# The data image comes first so it is the primary master, which is where the
# loader's ATA driver looks.
output=$(timeout "$timeout_seconds" qemu-system-x86_64 \
    -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code" \
    -drive "if=pflash,format=raw,file=$work/vars.fd" \
    -drive "format=raw,file=$data_image" \
    -drive "file=fat:rw:$work/esp,format=raw" \
    -m "$memory_mib" \
    -serial stdio -display none -net none -no-reboot 2>&1 || true)

printf '%s\n' "$output"

fail() {
    echo "uefi test: $1" >&2
    exit 1
}

require() {
    printf '%s' "$output" | grep -q "$2" || fail "$1"
}

require "the core never ran under uefi" "tunix-boot on uefi firmware"

# The map came from GetMemoryMap rather than E820 and is far more detailed, but
# everything above it is the same code the BIOS build runs.
require "the firmware memory map was not usable" "memory: [1-9][0-9]* MiB usable"
require "no arena was placed" "arena at [0-9a-f][0-9a-f]*"

# The loader painted the screen and read its own pixels back. Nobody can see a
# screen from a serial log, but a wrong framebuffer address or pitch produces a
# picture that is wrong in exactly the way this catches.
require "no framebuffer was acquired" "screen [1-9][0-9]*x[1-9][0-9]*, 32 bpp"
require "the screen did not read back as drawn" "screen readback ok"
require "no terminal was opened on the screen" "terminal [1-9][0-9]*x[1-9][0-9]* characters"

require "the ata driver found no disk" "disk ata, [1-9][0-9]* sectors"
require "the filesystem did not mount" "fat32 at lba 2048"
require "the configuration was not read" "config: 1 entries"
require "the kernel was not loaded" "kernel loaded, entering at ffffffff80000000"
require "the kernel's requests were not answered" "answered 6 kernel requests"

# Printed by the kernel, so the whole chain ran under firmware the loader was
# not written against.
require "the kernel did not run" "^kernel running$"
require "the kernel found its bss dirty" "^kernel bss was zeroed$"
require "the kernel did not get its command line" \
    "^cmdline: root=/dev/sda1 quiet$"

# The screen the loader acquired reached the kernel. The same kernel prints the
# other line on the BIOS path, where there is no VBE yet.
require "the kernel was not given the framebuffer" "^framebuffer received$"

# The firmware description the loader found, handed on so the kernel does not
# walk the tables a second time before it has to.
require "the kernel was not given the acpi tables" "^acpi tables received$"
require "the kernel was not given the processor list" \
    "^processor list received$"
require "no processors were found" "[1-9][0-9]* processors, [1-9][0-9]* startable"
require "no pci devices were enumerated" "pci: [1-9][0-9]* devices"

# The kernel is linked as three segments with different permissions, so this
# says the loader mapped them differently rather than making everything RWX.
require "the kernel was mapped without w^x" \
    "kernel mapping: 1 executable, 1 writable, nx on"

exit 0
