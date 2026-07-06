#!/bin/sh
# Boot the image and require the core to announce itself on the serial line.
# Everything before that line is the boot record, the disk read, the A20 gate,
# the page tables and the mode switch, so one marker covers the lot.
set -eu

image="$1"
marker="stage2 reached the core"
timeout_seconds=30

output=$(timeout "$timeout_seconds" qemu-system-x86_64 \
    -drive "format=raw,file=$image" \
    -serial stdio -display none -no-reboot 2>&1 || true)

printf '%s\n' "$output"
if printf '%s' "$output" | grep -q "$marker"; then
    exit 0
fi
echo "the core never reported reaching long mode" >&2
exit 1
