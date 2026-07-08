#ifndef TUNIX_BOOT_BLOCK_BLOCK_H
#define TUNIX_BOOT_BLOCK_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * One device, read only, addressed in sectors.
 *
 * Filesystems want byte ranges and hardware only moves whole sectors, so the
 * translation between the two happens here and exactly once. Every filesystem
 * added later reads through block_read_bytes and none of them repeat the
 * arithmetic, which is the arithmetic that gets silently wrong at the ends.
 *
 * Nothing here writes. A bootloader that can modify the disk it is booting from
 * is a bootloader that can destroy the system it failed to start.
 */

#define BLOCK_SECTOR_BYTES_DEFAULT 512U

/* Enough for the largest single request any caller below makes, and the reason
   a read is chunked rather than trusted to fit. */
#define BLOCK_MAX_SECTORS_PER_READ 64U

struct block_device;

/* Reads `count` whole sectors starting at `lba`. Implementations may assume
   count is non-zero and within BLOCK_MAX_SECTORS_PER_READ; the layer above
   splits anything larger. */
typedef bool (*block_read_sectors_fn)(const struct block_device *device,
                                      uint64_t lba, uint32_t count, void *out);

struct block_device {
    const char *name;
    block_read_sectors_fn read_sectors;

    uint32_t sector_bytes;
    /* Sectors on the device, or zero when the backing service will not say.
       Zero disables the range checks rather than failing every read. */
    uint64_t sector_count;

    /* Backend state. Not interpreted here. */
    void *context;

    /* One sector of scratch, so a byte range that starts or ends mid-sector can
       be served without allocating on a path that must not fail for want of
       memory. Callers never touch it. */
    uint8_t bounce[BLOCK_SECTOR_BYTES_DEFAULT];
};

bool block_device_valid(const struct block_device *device);

/* Whole sectors, splitting the request as far as the backend will take it. */
bool block_read(struct block_device *device, uint64_t lba, uint32_t count,
                void *out);

/* An arbitrary byte range, at any offset and of any length. */
bool block_read_bytes(struct block_device *device, uint64_t offset,
                      size_t length, void *out);

#endif
