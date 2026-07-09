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

require "the core never reached long mode" "stage2 reached the core"
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

exit 0
