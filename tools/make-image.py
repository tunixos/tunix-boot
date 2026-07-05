#!/usr/bin/env python3
"""Lay the boot record and stage2 out on a disk image.

The boot record occupies the first sector and reads a fixed number of sectors
after it, so the only thing this has to get right is that stage2 starts at LBA 1
and that the image is a whole number of sectors.
"""

import pathlib
import sys

SECTOR_BYTES = 512
BOOT_RECORD_SECTORS = 1
STAGE2_MAX_SECTORS = 64


def main():
    if len(sys.argv) != 4:
        print("usage: make-image.py <image> <stage1> <stage2>", file=sys.stderr)
        return 2

    image, stage1, stage2 = (pathlib.Path(argument) for argument in sys.argv[1:])
    boot_record = stage1.read_bytes()
    loader = stage2.read_bytes()

    if len(boot_record) != SECTOR_BYTES:
        print(f"boot record is {len(boot_record)} bytes, not {SECTOR_BYTES}",
              file=sys.stderr)
        return 1

    sectors = (len(loader) + SECTOR_BYTES - 1) // SECTOR_BYTES
    if sectors > STAGE2_MAX_SECTORS:
        print(f"stage2 needs {sectors} sectors, more than the boot record reads "
              f"({STAGE2_MAX_SECTORS})", file=sys.stderr)
        return 1

    # Padded out to everything the boot record asks for. Its read is a fixed
    # sector count, and a read that runs past the end of the disk fails, which
    # is the whole of stage2 failing for want of a few kilobytes of zeroes.
    total_bytes = (BOOT_RECORD_SECTORS + STAGE2_MAX_SECTORS) * SECTOR_BYTES
    padding = total_bytes - SECTOR_BYTES - len(loader)
    image.write_bytes(boot_record + loader + b"\x00" * padding)
    return 0


if __name__ == "__main__":
    sys.exit(main())
