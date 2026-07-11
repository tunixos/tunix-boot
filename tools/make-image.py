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
STAGE2_MAX_SECTORS = 64

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

# Read back by the loader, which checks these exact bytes. Changing the text
# means changing tests/qemu/boot-test.sh with it.
PAYLOAD_NAME = b"TUNIX   CFG"
PAYLOAD = b"tunix-boot filesystem check\n"


def put_u16(buffer, offset, value):
    buffer[offset:offset + 2] = value.to_bytes(2, "little")


def put_u32(buffer, offset, value):
    buffer[offset:offset + 4] = value.to_bytes(4, "little")


def build_fat32():
    """A FAT32 volume holding one file in its root directory."""
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

    file_clusters = (len(PAYLOAD) + SECTOR_BYTES * SECTORS_PER_CLUSTER - 1) // (
        SECTOR_BYTES * SECTORS_PER_CLUSTER)
    if FIRST_FILE_CLUSTER + file_clusters > clusters:
        raise ValueError("the payload does not fit in the partition")

    def set_fat(cluster, value):
        for index in range(FAT_COUNT):
            base = (RESERVED_SECTORS + index * SECTORS_PER_FAT) * SECTOR_BYTES
            put_u32(volume, base + cluster * 4, value)

    # Clusters 0 and 1 are not clusters; their entries hold the media byte and
    # the end-of-chain marker, and a reader that walks into them is lost.
    set_fat(0, 0x0FFFFFF8)
    set_fat(1, FAT_ENTRY_END)
    set_fat(ROOT_CLUSTER, FAT_ENTRY_END)

    for index in range(file_clusters):
        cluster = FIRST_FILE_CLUSTER + index
        last = index == file_clusters - 1
        set_fat(cluster, FAT_ENTRY_END if last else cluster + 1)

    def cluster_offset(cluster):
        sector = data_start + (cluster - 2) * SECTORS_PER_CLUSTER
        return sector * SECTOR_BYTES

    payload_at = cluster_offset(FIRST_FILE_CLUSTER)
    volume[payload_at:payload_at + len(PAYLOAD)] = PAYLOAD

    entry = bytearray(DIRECTORY_ENTRY_BYTES)
    entry[0:11] = PAYLOAD_NAME
    entry[11] = ATTRIBUTE_ARCHIVE
    put_u16(entry, 20, FIRST_FILE_CLUSTER >> 16)
    put_u16(entry, 26, FIRST_FILE_CLUSTER & 0xFFFF)
    put_u32(entry, 28, len(PAYLOAD))

    root_at = cluster_offset(ROOT_CLUSTER)
    volume[root_at:root_at + DIRECTORY_ENTRY_BYTES] = entry
    return volume


def write_partition_entry(boot_record):
    entry = bytearray(TABLE_ENTRY_BYTES)
    entry[4] = PARTITION_TYPE_FAT32_LBA
    put_u32(entry, 8, PARTITION_START_LBA)
    put_u32(entry, 12, PARTITION_SECTORS)
    boot_record[TABLE_OFFSET:TABLE_OFFSET + TABLE_ENTRY_BYTES] = entry


def main():
    if len(sys.argv) != 4:
        print("usage: make-image.py <image> <stage1> <stage2>", file=sys.stderr)
        return 2

    image, stage1, stage2 = (pathlib.Path(argument) for argument in sys.argv[1:])
    boot_record = bytearray(stage1.read_bytes())
    loader = stage2.read_bytes()

    if len(boot_record) != SECTOR_BYTES:
        print(f"boot record is {len(boot_record)} bytes, not {SECTOR_BYTES}",
              file=sys.stderr)
        return 1

    if any(boot_record[TABLE_OFFSET:SIGNATURE_OFFSET]):
        print("stage1 has grown into the partition table", file=sys.stderr)
        return 1
    write_partition_entry(boot_record)

    sectors = (len(loader) + SECTOR_BYTES - 1) // SECTOR_BYTES
    if sectors > STAGE2_MAX_SECTORS:
        print(f"stage2 needs {sectors} sectors, more than the boot record reads "
              f"({STAGE2_MAX_SECTORS})", file=sys.stderr)
        return 1

    loader_sectors = BOOT_RECORD_SECTORS + STAGE2_MAX_SECTORS
    if loader_sectors > PARTITION_START_LBA:
        print("the loader area runs into the partition", file=sys.stderr)
        return 1

    # Padded out to everything the boot record asks for. Its read is a fixed
    # sector count, and a read that runs past the end of the disk fails, which
    # is the whole of stage2 failing for want of a few kilobytes of zeroes.
    contents = bytearray(PARTITION_START_LBA * SECTOR_BYTES)
    contents[0:SECTOR_BYTES] = boot_record
    contents[SECTOR_BYTES:SECTOR_BYTES + len(loader)] = loader
    contents += build_fat32()

    image.write_bytes(bytes(contents))
    return 0


if __name__ == "__main__":
    sys.exit(main())
