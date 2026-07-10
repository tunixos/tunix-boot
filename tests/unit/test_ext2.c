#include "harness.h"
#include "fs/ext2.h"
#include "util/mem.h"

#define SECTOR_BYTES BLOCK_SECTOR_BYTES_DEFAULT
#define BLOCK_BYTES 1024U
#define BLOCK_COUNT 1024U
#define IMAGE_BYTES (BLOCK_COUNT * BLOCK_BYTES)

#define FIRST_DATA_BLOCK 1U
#define SUPERBLOCK_BLOCK 1U
#define GROUP_TABLE_BLOCK 2U
#define INODE_TABLE_BLOCK 5U
#define INODES_PER_GROUP 32U
#define INODE_BYTES 128U
#define FIRST_DATA_AREA_BLOCK 9U

/*
 * An ext2 volume built here for the same reason the FAT one is: the cases that
 * matter are an indirect pointer aimed past the end of the volume, a directory
 * record that does not advance, and an inode whose size disagrees with the
 * blocks behind it. mke2fs produces none of them.
 */
static uint8_t image[IMAGE_BYTES];
static uint32_t next_free_block;
static uint32_t next_free_inode;

static struct block_device device;
static struct ext2_volume volume;

static bool image_read(const struct block_device *self, uint64_t lba,
                       uint32_t count, void *out) {
    (void)self;
    if ((lba + count) * SECTOR_BYTES > IMAGE_BYTES) return false;
    memcpy(out, image + lba * SECTOR_BYTES, (size_t)count * SECTOR_BYTES);
    return true;
}

static void put_u16(uint8_t *at, uint16_t value) {
    at[0] = (uint8_t)(value & 0xFFU);
    at[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *at, uint32_t value) {
    for (unsigned index = 0; index < 4; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static uint8_t *block_at(uint32_t block) {
    return image + (size_t)block * BLOCK_BYTES;
}

static uint8_t *inode_at(uint32_t inode) {
    return block_at(INODE_TABLE_BLOCK) + (size_t)(inode - 1U) * INODE_BYTES;
}

static void format(void) {
    memset(image, 0, sizeof image);

    uint8_t *super = image + EXT2_SUPERBLOCK_OFFSET;
    put_u32(super + EXT2_SUPER_INODE_COUNT_OFFSET, INODES_PER_GROUP);
    put_u32(super + EXT2_SUPER_BLOCK_COUNT_OFFSET, BLOCK_COUNT);
    put_u32(super + EXT2_SUPER_FIRST_DATA_BLOCK_OFFSET, FIRST_DATA_BLOCK);
    put_u32(super + EXT2_SUPER_LOG_BLOCK_SIZE_OFFSET, 0);
    put_u32(super + EXT2_SUPER_BLOCKS_PER_GROUP_OFFSET, 8192);
    put_u32(super + EXT2_SUPER_INODES_PER_GROUP_OFFSET, INODES_PER_GROUP);
    put_u16(super + EXT2_SUPER_MAGIC_OFFSET, EXT2_MAGIC);
    put_u32(super + EXT2_SUPER_REVISION_OFFSET, 1);
    put_u16(super + EXT2_SUPER_INODE_BYTES_OFFSET, INODE_BYTES);

    put_u32(block_at(GROUP_TABLE_BLOCK) + EXT2_GROUP_INODE_TABLE_OFFSET,
            INODE_TABLE_BLOCK);

    next_free_block = FIRST_DATA_AREA_BLOCK;
    next_free_inode = EXT2_ROOT_INODE + 1U;

    /* The root directory: one empty block, grown by add_entry. */
    uint8_t *root = inode_at(EXT2_ROOT_INODE);
    put_u16(root + EXT2_INODE_MODE_OFFSET, EXT2_MODE_DIRECTORY);
    put_u32(root + EXT2_INODE_SIZE_LOW_OFFSET, 0);
    put_u32(root + EXT2_INODE_BLOCKS_OFFSET, next_free_block++);

    device.name = "image";
    device.read_sectors = image_read;
    device.sector_bytes = SECTOR_BYTES;
    device.sector_count = IMAGE_BYTES / SECTOR_BYTES;
    device.context = NULL;
}

static uint32_t inode_size(uint32_t inode) {
    const uint8_t *at = inode_at(inode) + EXT2_INODE_SIZE_LOW_OFFSET;
    return (uint32_t)(at[0] | (at[1] << 8) | (at[2] << 16) |
                      ((uint32_t)at[3] << 24));
}

static uint32_t inode_block(uint32_t inode, unsigned slot) {
    const uint8_t *at = inode_at(inode) + EXT2_INODE_BLOCKS_OFFSET + slot * 4U;
    return (uint32_t)(at[0] | (at[1] << 8) | (at[2] << 16) |
                      ((uint32_t)at[3] << 24));
}

/* Appends a directory record. Every directory here fits in one block, so the
   last record is extended to fill it exactly, as ext2 requires. */
static void add_entry(uint32_t directory, const char *name, uint32_t inode) {
    uint32_t block = inode_block(directory, 0);
    uint32_t used = inode_size(directory);

    size_t length = 0;
    while (name[length] != '\0') length++;

    uint32_t record = (uint32_t)(EXT2_DIRENT_MIN_BYTES + length);
    record = (record + 3U) & ~3U;

    uint8_t *at = block_at(block) + used;
    put_u32(at + EXT2_DIRENT_INODE_OFFSET, inode);
    put_u16(at + EXT2_DIRENT_RECORD_LENGTH_OFFSET, (uint16_t)record);
    at[EXT2_DIRENT_NAME_LENGTH_OFFSET] = (uint8_t)length;
    for (size_t index = 0; index < length; index++)
        at[EXT2_DIRENT_NAME_OFFSET + index] = (uint8_t)name[index];

    put_u32(inode_at(directory) + EXT2_INODE_SIZE_LOW_OFFSET, used + record);
}

static uint8_t content_byte(size_t index) {
    return (uint8_t)(index * 53U + (index >> 7) * 29U + 11U);
}

/* Writes a file of `size` bytes, allocating direct, indirect and double
   indirect blocks as the size demands. */
static uint32_t write_file(uint32_t directory, const char *name, uint32_t size) {
    uint32_t inode = next_free_inode++;
    uint8_t *node = inode_at(inode);
    put_u16(node + EXT2_INODE_MODE_OFFSET, EXT2_MODE_REGULAR);
    put_u32(node + EXT2_INODE_SIZE_LOW_OFFSET, size);

    const uint32_t per_block = BLOCK_BYTES / EXT2_POINTER_BYTES;
    uint32_t blocks = (size + BLOCK_BYTES - 1U) / BLOCK_BYTES;
    uint32_t indirect = 0;
    uint32_t double_indirect = 0;
    uint32_t double_leaf = 0;

    for (uint32_t index = 0; index < blocks; index++) {
        uint32_t data = next_free_block++;
        for (uint32_t byte = 0; byte < BLOCK_BYTES; byte++) {
            size_t position = (size_t)index * BLOCK_BYTES + byte;
            if (position < size) block_at(data)[byte] = content_byte(position);
        }

        if (index < EXT2_DIRECT_BLOCKS) {
            put_u32(node + EXT2_INODE_BLOCKS_OFFSET + index * 4U, data);
        } else if (index < EXT2_DIRECT_BLOCKS + per_block) {
            if (indirect == 0) {
                indirect = next_free_block++;
                put_u32(node + EXT2_INODE_BLOCKS_OFFSET +
                            EXT2_INDIRECT_INDEX * 4U, indirect);
            }
            put_u32(block_at(indirect) +
                        (index - EXT2_DIRECT_BLOCKS) * EXT2_POINTER_BYTES, data);
        } else {
            uint32_t within = index - EXT2_DIRECT_BLOCKS - per_block;
            if (double_indirect == 0) {
                double_indirect = next_free_block++;
                put_u32(node + EXT2_INODE_BLOCKS_OFFSET +
                            EXT2_DOUBLE_INDIRECT_INDEX * 4U, double_indirect);
            }
            if (within % per_block == 0) {
                double_leaf = next_free_block++;
                put_u32(block_at(double_indirect) +
                            (within / per_block) * EXT2_POINTER_BYTES, double_leaf);
            }
            put_u32(block_at(double_leaf) +
                        (within % per_block) * EXT2_POINTER_BYTES, data);
        }
    }

    add_entry(directory, name, inode);
    return inode;
}

static uint32_t make_directory(uint32_t parent, const char *name) {
    uint32_t inode = next_free_inode++;
    uint8_t *node = inode_at(inode);
    put_u16(node + EXT2_INODE_MODE_OFFSET, EXT2_MODE_DIRECTORY);
    put_u32(node + EXT2_INODE_SIZE_LOW_OFFSET, 0);
    put_u32(node + EXT2_INODE_BLOCKS_OFFSET, next_free_block++);
    add_entry(parent, name, inode);
    return inode;
}

static bool content_matches(const uint8_t *got, size_t offset, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (got[index] != content_byte(offset + index)) return false;
    }
    return true;
}

static void mounts_a_volume(void) {
    format();
    CHECK(ext2_mount(&device, &volume));
    CHECK(volume.block_bytes == BLOCK_BYTES);
    CHECK(volume.block_count == BLOCK_COUNT);
    CHECK(volume.inode_bytes == INODE_BYTES);
    CHECK(volume.group_table_offset == GROUP_TABLE_BLOCK * BLOCK_BYTES);
}

static void refuses_what_is_not_an_ext2_volume(void) {
    format();
    put_u16(image + EXT2_SUPERBLOCK_OFFSET + EXT2_SUPER_MAGIC_OFFSET, 0);
    CHECK(!ext2_mount(&device, &volume));

    format();
    put_u32(image + EXT2_SUPERBLOCK_OFFSET + EXT2_SUPER_LOG_BLOCK_SIZE_OFFSET, 99);
    CHECK(!ext2_mount(&device, &volume));

    format();
    put_u32(image + EXT2_SUPERBLOCK_OFFSET + EXT2_SUPER_INODES_PER_GROUP_OFFSET, 0);
    CHECK(!ext2_mount(&device, &volume));

    /* An extent-mapped volume stores its block map differently, and reading it
       as a block list produces plausible garbage. */
    format();
    put_u32(image + EXT2_SUPERBLOCK_OFFSET + EXT2_SUPER_INCOMPATIBLE_OFFSET,
            EXT2_INCOMPATIBLE_EXTENTS);
    CHECK(!ext2_mount(&device, &volume));
}

static void a_volume_bigger_than_its_disk_is_refused(void) {
    format();
    put_u32(image + EXT2_SUPERBLOCK_OFFSET + EXT2_SUPER_BLOCK_COUNT_OFFSET,
            BLOCK_COUNT * 8U);
    CHECK(!ext2_mount(&device, &volume));
}

static void reads_a_file_from_direct_blocks(void) {
    static uint8_t got[3 * BLOCK_BYTES + 40];
    format();
    write_file(EXT2_ROOT_INODE, "hello.txt", sizeof got);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/hello.txt", &file));
    CHECK(file.size == sizeof got);
    CHECK(!file.directory);
    CHECK(ext2_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));

    /* Unaligned, straddling a block edge. */
    const uint64_t offset = BLOCK_BYTES - 7;
    CHECK(ext2_read(&file, offset, 64, got));
    CHECK(content_matches(got, offset, 64));
}

static void reads_a_file_through_an_indirect_block(void) {
    static uint8_t got[20 * BLOCK_BYTES];
    format();
    write_file(EXT2_ROOT_INODE, "medium.bin", sizeof got);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/medium.bin", &file));
    CHECK(ext2_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));

    /* The first block past the twelve direct ones is where indirection starts. */
    const uint64_t offset = EXT2_DIRECT_BLOCKS * BLOCK_BYTES;
    CHECK(ext2_read(&file, offset, BLOCK_BYTES, got));
    CHECK(content_matches(got, offset, BLOCK_BYTES));
}

/* Past 12 + 256 blocks the map needs two levels, and a kernel image is easily
   big enough to get there. Getting the depth wrong reads a pointer block as if
   it were data. */
static void reads_a_file_through_double_indirection(void) {
    static uint8_t got[4 * BLOCK_BYTES];
    const uint32_t per_block = BLOCK_BYTES / EXT2_POINTER_BYTES;
    const uint32_t first_double = EXT2_DIRECT_BLOCKS + per_block;
    const uint32_t size = (first_double + 4U) * BLOCK_BYTES;

    format();
    write_file(EXT2_ROOT_INODE, "kernel.img", size);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/kernel.img", &file));
    CHECK(file.size == size);

    const uint64_t offset = (uint64_t)first_double * BLOCK_BYTES;
    CHECK(ext2_read(&file, offset, sizeof got, got));
    CHECK(content_matches(got, offset, sizeof got));

    /* The last block of the single-indirect range still resolves, so the two
       levels have not been confused for one another. */
    CHECK(ext2_read(&file, offset - BLOCK_BYTES, BLOCK_BYTES, got));
    CHECK(content_matches(got, offset - BLOCK_BYTES, BLOCK_BYTES));
}

static void names_are_case_sensitive(void) {
    format();
    write_file(EXT2_ROOT_INODE, "Kernel.elf", 64);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/Kernel.elf", &file));
    /* Unlike FAT, ext2 stores the name as given and matches it exactly. */
    CHECK(!ext2_open(&volume, "/kernel.elf", &file));
    CHECK(!ext2_open(&volume, "/Kernel", &file));
}

static void walks_into_subdirectories(void) {
    static uint8_t got[64];
    format();
    uint32_t boot = make_directory(EXT2_ROOT_INODE, "boot");
    uint32_t nested = make_directory(boot, "nested");
    write_file(nested, "deep.cfg", sizeof got);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/boot/nested/deep.cfg", &file));
    CHECK(ext2_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));

    struct ext2_file directory;
    CHECK(ext2_open(&volume, "/boot", &directory));
    CHECK(directory.directory);
    CHECK(!ext2_read(&directory, 0, 1, got));

    CHECK(!ext2_open(&volume, "/boot/missing", &file));
    CHECK(!ext2_open(&volume, "/nothere/deep.cfg", &file));
}

static void refuses_to_read_past_the_end(void) {
    static uint8_t got[64];
    format();
    write_file(EXT2_ROOT_INODE, "short.bin", 40);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/short.bin", &file));
    CHECK(!ext2_read(&file, 0, 41, got));
    CHECK(!ext2_read(&file, 40, 1, got));
    CHECK(!ext2_read(&file, UINT64_MAX - 4, 8, got));
    CHECK(ext2_read(&file, 39, 1, got));
}

static void a_hole_reads_as_zeroes(void) {
    static uint8_t got[BLOCK_BYTES];
    format();
    uint32_t inode = write_file(EXT2_ROOT_INODE, "sparse.bin", 3 * BLOCK_BYTES);
    /* Sparse files are stored by not storing them; a zero pointer is a hole,
       not a failure. */
    put_u32(inode_at(inode) + EXT2_INODE_BLOCKS_OFFSET + 4U, 0);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/sparse.bin", &file));
    CHECK(ext2_read(&file, BLOCK_BYTES, sizeof got, got));
    for (size_t index = 0; index < sizeof got; index++) CHECK(got[index] == 0);

    /* The blocks either side of the hole still read normally. */
    CHECK(ext2_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));
}

static void a_pointer_outside_the_volume_is_refused(void) {
    static uint8_t got[BLOCK_BYTES];
    format();
    uint32_t inode = write_file(EXT2_ROOT_INODE, "bad.bin", 2 * BLOCK_BYTES);
    put_u32(inode_at(inode) + EXT2_INODE_BLOCKS_OFFSET, BLOCK_COUNT + 50U);
    CHECK(ext2_mount(&device, &volume));

    struct ext2_file file;
    CHECK(ext2_open(&volume, "/bad.bin", &file));
    /* One block past the end reads whatever follows the volume and looks like
       data, so it is refused rather than clamped. */
    CHECK(!ext2_read(&file, 0, sizeof got, got));
    CHECK(ext2_read(&file, BLOCK_BYTES, sizeof got, got));
}

static void a_directory_record_that_does_not_advance_is_refused(void) {
    format();
    write_file(EXT2_ROOT_INODE, "first.txt", 32);
    CHECK(ext2_mount(&device, &volume));

    /* A zero-length record is a directory that never ends. */
    uint32_t block = inode_block(EXT2_ROOT_INODE, 0);
    put_u16(block_at(block) + EXT2_DIRENT_RECORD_LENGTH_OFFSET, 0);

    struct ext2_file file;
    CHECK(!ext2_open(&volume, "/first.txt", &file));
    CHECK(!ext2_open(&volume, "/anything", &file));
}

static void a_record_reaching_past_the_directory_is_refused(void) {
    format();
    write_file(EXT2_ROOT_INODE, "first.txt", 32);
    CHECK(ext2_mount(&device, &volume));

    uint32_t block = inode_block(EXT2_ROOT_INODE, 0);
    put_u16(block_at(block) + EXT2_DIRENT_RECORD_LENGTH_OFFSET, 4096);

    struct ext2_file file;
    CHECK(!ext2_open(&volume, "/first.txt", &file));
}

static void an_inode_outside_the_table_is_refused(void) {
    format();
    write_file(EXT2_ROOT_INODE, "first.txt", 32);
    CHECK(ext2_mount(&device, &volume));

    uint32_t block = inode_block(EXT2_ROOT_INODE, 0);
    put_u32(block_at(block) + EXT2_DIRENT_INODE_OFFSET, INODES_PER_GROUP + 100U);

    struct ext2_file file;
    CHECK(!ext2_open(&volume, "/first.txt", &file));
}

TEST_MAIN(
    mounts_a_volume();
    refuses_what_is_not_an_ext2_volume();
    a_volume_bigger_than_its_disk_is_refused();
    reads_a_file_from_direct_blocks();
    reads_a_file_through_an_indirect_block();
    reads_a_file_through_double_indirection();
    names_are_case_sensitive();
    walks_into_subdirectories();
    refuses_to_read_past_the_end();
    a_hole_reads_as_zeroes();
    a_pointer_outside_the_volume_is_refused();
    a_directory_record_that_does_not_advance_is_refused();
    a_record_reaching_past_the_directory_is_refused();
    an_inode_outside_the_table_is_refused();
)
