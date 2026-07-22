#!/bin/sh
# Boot the image and require the core to report each thing it can only report if
# the step before it worked: the boot record, the disk read, the A20 gate, the
# page tables, the mode switch, the E820 walk, and a usable arena.
#
# The memory size is fixed here because the expected total is checked against it.
set -eu

image="$1"
memory_mib=2048
expected_usable_mib=2047
timeout_seconds=30

output=$(timeout "$timeout_seconds" qemu-system-x86_64 \
    -drive "format=raw,file=$image" \
    -m "$memory_mib" \
    -serial stdio -display none -no-reboot 2>&1 || true)

printf '%s\n' "$output"

fail() {
    echo "boot test: $1" >&2
    exit 1
}

require() {
    printf '%s' "$output" | grep -q "$2" || fail "$1"
}

require "the core never reached long mode" "the machine is described and readable"
require "long mode was not detected" "long mode yes"

# Nothing below this point can pass unless stage2 ran the E820 loop in real mode
# and the core parsed what it left behind.
require "no firmware memory entries were collected" \
    "boot drive 80, [1-9][0-9]* firmware memory entries"
require "the memory map did not add up to the machine" \
    "memory: $expected_usable_mib MiB usable"
if printf '%s' "$output" | grep -q "truncated"; then
    fail "the memory map did not fit and entries were dropped"
fi

require "no arena was placed" "arena at [0-9a-f][0-9a-f]*"

# The driver reads the image back off the disk it was loaded from, so the
# sector count is the image's own and the signature is the one stage1 carries.
require "the ata driver found no disk" "disk ata, [1-9][0-9]* sectors"
require "the boot record did not read back" \
    "boot record verified through the ata driver"

# The partition table, the partition window and the filesystem, ending in the
# exact bytes make-image.py wrote into the image. Nothing below this line can
# pass unless every layer beneath it addressed the disk correctly.
require "the partition table was not read" "[1-9][0-9]* partitions, mbr table"
require "the filesystem did not mount" "fat32 at lba 2048, [1-9][0-9]* clusters"
require "the configuration was not read and parsed" \
    "config: 1 entries, timeout 5, booting /kernel.elf"
require "the command line did not come out of the configuration" \
    "cmdline from config: root=/dev/sda1 quiet"

# The kernel is built by the real toolchain and entered for real. These last two
# lines are printed by the kernel itself, so nothing but a working loader can
# produce them: the second says the loader zeroed the .bss it was handed.
require "the kernel was not loaded" "kernel loaded, entering at ffffffff80000000"
require "no page tables were built" "page tables at [0-9a-f][0-9a-f]*"
require "the kernel page tables were not installed" \
    "kernel page tables active"

# The kernel is linked into the higher half and loaded into low physical memory,
# so it can only run at all if the tables the loader built are correct — and the
# loader can only still be alive to have jumped if it mapped itself too.
require "the kernel did not run" "^kernel running$"
require "the kernel found its bss dirty" "^kernel bss was zeroed$"

# The loader claims the memory it is using, so the map the kernel is handed
# describes the machine as it is now rather than as the firmware found it.
require "the loader did not claim its own memory" \
    "after claiming: [0-9]* MiB usable"
require "the kernel's requests were not answered" "answered 6 kernel requests"

# Printed by the kernel out of the responses it was given, so each line is a
# round trip: the kernel declared a request, the loader found it by scanning the
# image, and the pointer it wrote back was readable from the kernel's own map.
require "the kernel was not told who loaded it" "^loaded by tunix-boot "
require "the kernel did not get its command line" \
    "^cmdline: root=/dev/sda1 quiet$"
require "the kernel did not get a memory map" "^memory map received$"
if printf '%s' "$output" | grep -q "a request went unanswered"; then
    fail "the kernel found one of its responses null"
fi

# stage2 found a VBE mode and set it before leaving real mode, so this path has
# a screen too — and the loader read its own pixels back off it.
require "no vbe mode was set" "screen [1-9][0-9]*x[1-9][0-9]*, 32 bpp"
require "the screen did not read back as drawn" "screen readback ok"
require "no terminal was opened on the screen" "terminal [1-9][0-9]*x[1-9][0-9]* characters"
require "the kernel was not given the framebuffer" "^framebuffer received$"

# The firmware description the loader found, handed on so the kernel does not
# walk the tables a second time before it has to.
require "the kernel was not given the acpi tables" "^acpi tables received$"
require "the kernel was not given the processor list" \
    "^processor list received$"
require "no processors were found" "[1-9][0-9]* processors, [1-9][0-9]* startable"
require "no pci devices were enumerated" "pci: [1-9][0-9]* devices"

exit 0
