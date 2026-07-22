#!/usr/bin/env python3
"""Lay the boot record, stage2 and a FAT32 partition out on a disk image.

The boot record occupies the first sector and reads a fixed number of sectors
after it, so the only thing the loader area has to get right is that stage2
starts at LBA 1 and that the image is a whole number of sectors.

The filesystem is built here rather than by mkfs so the build needs no external
tool and no privileges, and so the image is byte-for-byte reproducible.
"""

import pathlib
import sys

SECTOR_BYTES = 512
BOOT_RECORD_SECTORS = 1
# stage1 reads this in chunks of 64, so the only real bound is that the loader
# area must stay clear of the partition.
STAGE2_MAX_SECTORS = 256

# Where the partition table lives inside the boot record. stage1 is well short
# of this; make-image refuses to write over it if that ever stops being true.
TABLE_OFFSET = 446
TABLE_ENTRY_BYTES = 16
SIGNATURE_OFFSET = 510
SIGNATURE = 0xAA55

PARTITION_START_LBA = 2048
PARTITION_SECTORS = 4096
PARTITION_TYPE_FAT32_LBA = 0x0C

RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
SECTORS_PER_FAT = 32
ROOT_CLUSTER = 2
FIRST_FILE_CLUSTER = 3

FAT_ENTRY_END = 0x0FFFFFFF
DIRECTORY_ENTRY_BYTES = 32
ATTRIBUTE_ARCHIVE = 0x20

# The configuration the loader reads to decide what to boot. Parsed for real, so
# the kernel path and command line below are what actually reach the kernel;
# tests/qemu/boot-test.sh checks the command line comes back out of it.
CONFIG_NAME = b"TUNIX   CFG"
CONFIG = b"""# what to boot
timeout = 5
default = tunix

:tunix
kernel = /kernel.elf
cmdline = root=/dev/sda1 quiet
"""
KERNEL_NAME = b"KERNEL  ELF"


def put_u16(buffer, offset, value):
    buffer[offset:offset + 2] = value.to_bytes(2, "little")


def put_u32(buffer, offset, value):
    buffer[offset:offset + 4] = value.to_bytes(4, "little")


def build_fat32(files):
    """A FAT32 volume holding `files`, a list of (8.3 name, contents)."""
    volume = bytearray(PARTITION_SECTORS * SECTOR_BYTES)

    put_u16(volume, 11, SECTOR_BYTES)
    volume[13] = SECTORS_PER_CLUSTER
    put_u16(volume, 14, RESERVED_SECTORS)
    volume[16] = FAT_COUNT
    put_u16(volume, 17, 0)                      # zero for FAT32
    put_u16(volume, 19, 0)                      # the 32-bit count is used
    volume[21] = 0xF8                           # fixed disk
    put_u16(volume, 22, 0)                      # zero for FAT32
    put_u32(volume, 32, PARTITION_SECTORS)
    put_u32(volume, 36, SECTORS_PER_FAT)
    put_u32(volume, 44, ROOT_CLUSTER)
    put_u16(volume, SIGNATURE_OFFSET, SIGNATURE)

    data_start = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
    if data_start >= PARTITION_SECTORS:
        raise ValueError("the reserved area and FATs leave no room for data")

    clusters = PARTITION_SECTORS - data_start
    if (clusters + 2) * 4 > SECTORS_PER_FAT * SECTOR_BYTES:
        raise ValueError("SECTORS_PER_FAT is too small for the cluster count")

    def set_fat(cluster, value):
        for index in range(FAT_COUNT):
            base = (RESERVED_SECTORS + index * SECTORS_PER_FAT) * SECTOR_BYTES
            put_u32(volume, base + cluster * 4, value)

    # Clusters 0 and 1 are not clusters; their entries hold the media byte and
    # the end-of-chain marker, and a reader that walks into them is lost.
    set_fat(0, 0x0FFFFFF8)
    set_fat(1, FAT_ENTRY_END)
    set_fat(ROOT_CLUSTER, FAT_ENTRY_END)

    def cluster_offset(cluster):
        sector = data_start + (cluster - 2) * SECTORS_PER_CLUSTER
        return sector * SECTOR_BYTES

    cluster_bytes = SECTOR_BYTES * SECTORS_PER_CLUSTER
    root_at = cluster_offset(ROOT_CLUSTER)
    next_cluster = FIRST_FILE_CLUSTER

    for slot, (name, contents) in enumerate(files):
        if (slot + 1) * DIRECTORY_ENTRY_BYTES > cluster_bytes:
            raise ValueError("the root directory holds one cluster of entries")

        needed = max(1, (len(contents) + cluster_bytes - 1) // cluster_bytes)
        if next_cluster + needed > clusters:
            raise ValueError(f"{name!r} does not fit in the partition")

        first = next_cluster
        for index in range(needed):
            cluster = first + index
            last = index == needed - 1
            set_fat(cluster, FAT_ENTRY_END if last else cluster + 1)
            at = cluster_offset(cluster)
            piece = contents[index * cluster_bytes:(index + 1) * cluster_bytes]
            volume[at:at + len(piece)] = piece
        next_cluster += needed

        entry = bytearray(DIRECTORY_ENTRY_BYTES)
        entry[0:11] = name
        entry[11] = ATTRIBUTE_ARCHIVE
        put_u16(entry, 20, first >> 16)
        put_u16(entry, 26, first & 0xFFFF)
        put_u32(entry, 28, len(contents))

        at = root_at + slot * DIRECTORY_ENTRY_BYTES
        volume[at:at + DIRECTORY_ENTRY_BYTES] = entry

    return volume


def write_partition_entry(boot_record):
    entry = bytearray(TABLE_ENTRY_BYTES)
    entry[4] = PARTITION_TYPE_FAT32_LBA
    put_u32(entry, 8, PARTITION_START_LBA)
    put_u32(entry, 12, PARTITION_SECTORS)
    boot_record[TABLE_OFFSET:TABLE_OFFSET + TABLE_ENTRY_BYTES] = entry


def main():
    # Under UEFI there is no boot record to lay down and no stage2 to load: the
    # firmware loads the application itself, and all this image has to carry is
    # the partition the loader then reads. The rest of it is identical, which is
    # the point — one script, and the same filesystem either way.
    if len(sys.argv) == 3:
        image, kernel = (pathlib.Path(argument) for argument in sys.argv[1:])
        boot_record = bytearray(SECTOR_BYTES)
        put_u16(boot_record, SIGNATURE_OFFSET, SIGNATURE)
        loader = b""
    elif len(sys.argv) == 5:
        image, stage1, stage2, kernel = (
            pathlib.Path(argument) for argument in sys.argv[1:])
        boot_record = bytearray(stage1.read_bytes())
        loader = stage2.read_bytes()

        if len(boot_record) != SECTOR_BYTES:
            print(f"boot record is {len(boot_record)} bytes, not {SECTOR_BYTES}",
                  file=sys.stderr)
            return 1
        if any(boot_record[TABLE_OFFSET:SIGNATURE_OFFSET]):
            print("stage1 has grown into the partition table", file=sys.stderr)
            return 1

        sectors = (len(loader) + SECTOR_BYTES - 1) // SECTOR_BYTES
        if sectors > STAGE2_MAX_SECTORS:
            print(f"stage2 needs {sectors} sectors, more than the boot record "
                  f"reads ({STAGE2_MAX_SECTORS})", file=sys.stderr)
            return 1
    else:
        print("usage: make-image.py <image> [<stage1> <stage2>] <kernel>",
              file=sys.stderr)
        return 2

    write_partition_entry(boot_record)

    if BOOT_RECORD_SECTORS + STAGE2_MAX_SECTORS > PARTITION_START_LBA:
        print("the loader area runs into the partition", file=sys.stderr)
        return 1

    # Padded out to everything the boot record asks for. Its read is a fixed
    # sector count, and a read that runs past the end of the disk fails, which
    # is the whole of stage2 failing for want of a few kilobytes of zeroes.
    contents = bytearray(PARTITION_START_LBA * SECTOR_BYTES)
    contents[0:SECTOR_BYTES] = boot_record
    contents[SECTOR_BYTES:SECTOR_BYTES + len(loader)] = loader
    contents += build_fat32([
        (CONFIG_NAME, CONFIG),
        (KERNEL_NAME, kernel.read_bytes()),
    ])

    image.write_bytes(bytes(contents))
    return 0


if __name__ == "__main__":
    sys.exit(main())
