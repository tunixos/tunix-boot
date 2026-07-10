#include "harness.h"
#include "fs/fat.h"
#include "util/mem.h"

#define SECTOR_BYTES BLOCK_SECTOR_BYTES_DEFAULT
#define IMAGE_SECTORS 512U
#define IMAGE_BYTES (IMAGE_SECTORS * SECTOR_BYTES)

#define RESERVED_SECTORS 32U
#define SECTORS_PER_FAT 4U
#define SECTORS_PER_CLUSTER 1U
#define DATA_START_SECTOR (RESERVED_SECTORS + SECTORS_PER_FAT)
#define ROOT_CLUSTER 2U

/*
 * A FAT32 volume assembled here rather than by mkfs, because the cases worth
 * testing are the ones a working tool never produces: a chain that loops, a
 * cluster number past the end of the volume, a size that disagrees with the
 * chain behind it.
 */
static uint8_t image[IMAGE_BYTES];
static uint32_t next_free_cluster;

static struct block_device device;
static struct fat_volume volume;

static bool image_read(const struct block_device *self, uint64_t lba,
                       uint32_t count, void *out) {
    (void)self;
    if (lba + count > IMAGE_SECTORS) return false;
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

static void set_fat(uint32_t cluster, uint32_t value) {
    put_u32(image + RESERVED_SECTORS * SECTOR_BYTES + cluster * FAT_FAT_ENTRY_BYTES,
            value);
}

static uint8_t *cluster_data(uint32_t cluster) {
    return image + (DATA_START_SECTOR + (cluster - FAT_FIRST_DATA_CLUSTER) *
                                            SECTORS_PER_CLUSTER) * SECTOR_BYTES;
}

static void format(void) {
    memset(image, 0, sizeof image);

    uint8_t *bpb = image;
    put_u16(bpb + FAT_BPB_BYTES_PER_SECTOR_OFFSET, SECTOR_BYTES);
    bpb[FAT_BPB_SECTORS_PER_CLUSTER_OFFSET] = SECTORS_PER_CLUSTER;
    put_u16(bpb + FAT_BPB_RESERVED_SECTORS_OFFSET, RESERVED_SECTORS);
    bpb[FAT_BPB_FAT_COUNT_OFFSET] = 1;
    put_u16(bpb + FAT_BPB_ROOT_ENTRY_COUNT_OFFSET, 0);
    put_u16(bpb + FAT_BPB_SECTORS_PER_FAT_16_OFFSET, 0);
    put_u32(bpb + FAT_BPB_TOTAL_SECTORS_32_OFFSET, IMAGE_SECTORS);
    put_u32(bpb + FAT_BPB_SECTORS_PER_FAT_32_OFFSET, SECTORS_PER_FAT);
    put_u32(bpb + FAT_BPB_ROOT_CLUSTER_OFFSET, ROOT_CLUSTER);
    put_u16(bpb + FAT_BPB_SIGNATURE_OFFSET, FAT_BPB_SIGNATURE);

    set_fat(ROOT_CLUSTER, FAT_CLUSTER_END);
    next_free_cluster = ROOT_CLUSTER + 1U;

    device.name = "image";
    device.read_sectors = image_read;
    device.sector_bytes = SECTOR_BYTES;
    device.sector_count = IMAGE_SECTORS;
    device.context = NULL;
}

/* Appends a directory entry to a directory that occupies one cluster. */
static uint8_t *append_entry(uint32_t directory_cluster, const char *name,
                             uint8_t attributes, uint32_t first_cluster,
                             uint32_t size) {
    uint8_t *at = cluster_data(directory_cluster);
    while (at[FAT_ENTRY_NAME_OFFSET] != FAT_ENTRY_END_OF_DIRECTORY)
        at += FAT_DIRECTORY_ENTRY_BYTES;

    for (unsigned index = 0; index < FAT_ENTRY_NAME_BYTES; index++)
        at[index] = (uint8_t)name[index];
    at[FAT_ENTRY_ATTRIBUTES_OFFSET] = attributes;
    put_u16(at + FAT_ENTRY_CLUSTER_HIGH_OFFSET, (uint16_t)(first_cluster >> 16));
    put_u16(at + FAT_ENTRY_CLUSTER_LOW_OFFSET, (uint16_t)(first_cluster & 0xFFFFU));
    put_u32(at + FAT_ENTRY_SIZE_OFFSET, size);
    return at;
}

static uint8_t content_byte(size_t index) {
    return (uint8_t)(index * 37U + (index >> 8) * 11U + 5U);
}

/* Writes `size` bytes across as many clusters as it takes, chaining them. */
static uint32_t write_file(uint32_t directory_cluster, const char *name,
                           uint32_t size) {
    uint32_t first = next_free_cluster;
    uint32_t cluster = first;
    uint32_t written = 0;

    while (written < size) {
        uint32_t take = size - written;
        if (take > SECTOR_BYTES * SECTORS_PER_CLUSTER)
            take = SECTOR_BYTES * SECTORS_PER_CLUSTER;
        for (uint32_t index = 0; index < take; index++)
            cluster_data(cluster)[index] = content_byte(written + index);
        written += take;

        if (written < size) {
            set_fat(cluster, cluster + 1U);
            cluster++;
        } else {
            set_fat(cluster, FAT_CLUSTER_END);
        }
    }
    next_free_cluster = cluster + 1U;
    append_entry(directory_cluster, name, 0, first, size);
    return first;
}

static uint32_t make_directory(uint32_t parent_cluster, const char *name) {
    uint32_t cluster = next_free_cluster++;
    set_fat(cluster, FAT_CLUSTER_END);
    append_entry(parent_cluster, name, FAT_ATTRIBUTE_DIRECTORY, cluster, 0);
    return cluster;
}

static bool content_matches(const uint8_t *got, size_t offset, size_t length) {
    for (size_t index = 0; index < length; index++) {
        if (got[index] != content_byte(offset + index)) return false;
    }
    return true;
}

static void mounts_a_volume(void) {
    format();
    CHECK(fat_mount(&device, &volume));
    CHECK(volume.cluster_bytes == SECTOR_BYTES * SECTORS_PER_CLUSTER);
    CHECK(volume.root_cluster == ROOT_CLUSTER);
    CHECK(volume.data_start_sector == DATA_START_SECTOR);
}

static void refuses_what_is_not_a_fat32_volume(void) {
    format();
    put_u16(image + FAT_BPB_SIGNATURE_OFFSET, 0);
    CHECK(!fat_mount(&device, &volume));

    /* A non-zero root entry count is FAT12 or FAT16, which this does not read. */
    format();
    put_u16(image + FAT_BPB_ROOT_ENTRY_COUNT_OFFSET, 512);
    CHECK(!fat_mount(&device, &volume));

    format();
    put_u16(image + FAT_BPB_SECTORS_PER_FAT_16_OFFSET, 32);
    CHECK(!fat_mount(&device, &volume));

    format();
    image[FAT_BPB_SECTORS_PER_CLUSTER_OFFSET] = 0;
    CHECK(!fat_mount(&device, &volume));

    /* Not a power of two: the cluster arithmetic stops being addressable. */
    format();
    image[FAT_BPB_SECTORS_PER_CLUSTER_OFFSET] = 3;
    CHECK(!fat_mount(&device, &volume));

    format();
    image[FAT_BPB_FAT_COUNT_OFFSET] = 0;
    CHECK(!fat_mount(&device, &volume));
}

static void a_volume_bigger_than_its_disk_is_refused(void) {
    format();
    put_u32(image + FAT_BPB_TOTAL_SECTORS_32_OFFSET, IMAGE_SECTORS * 4U);
    CHECK(!fat_mount(&device, &volume));
}

static void reads_a_file_in_the_root(void) {
    static uint8_t got[100];
    format();
    write_file(ROOT_CLUSTER, "HELLO   TXT", sizeof got);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/hello.txt", &file));
    CHECK(file.size == sizeof got);
    CHECK(!file.directory);

    CHECK(fat_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));
}

static void matches_names_without_regard_to_case(void) {
    format();
    write_file(ROOT_CLUSTER, "KERNEL  ELF", 64);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/kernel.elf", &file));
    CHECK(fat_open(&volume, "/KERNEL.ELF", &file));
    CHECK(fat_open(&volume, "/KeRnEl.eLf", &file));
    CHECK(!fat_open(&volume, "/kernel.el", &file));
    CHECK(!fat_open(&volume, "/kernel", &file));
}

static void reads_a_file_spanning_several_clusters(void) {
    static uint8_t got[5 * SECTOR_BYTES + 17];
    format();
    write_file(ROOT_CLUSTER, "BIG     BIN", sizeof got);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/big.bin", &file));
    CHECK(file.size == sizeof got);

    /* The whole file, then an unaligned window straddling a cluster edge. */
    CHECK(fat_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));

    const uint64_t offset = SECTOR_BYTES - 9;
    CHECK(fat_read(&file, offset, 64, got));
    CHECK(content_matches(got, offset, 64));
}

static void walks_into_subdirectories(void) {
    static uint8_t got[48];
    format();
    uint32_t boot = make_directory(ROOT_CLUSTER, "BOOT       ");
    uint32_t nested = make_directory(boot, "NESTED     ");
    write_file(nested, "DEEP    CFG", sizeof got);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/boot/nested/deep.cfg", &file));
    CHECK(fat_read(&file, 0, sizeof got, got));
    CHECK(content_matches(got, 0, sizeof got));

    struct fat_file directory;
    CHECK(fat_open(&volume, "/boot", &directory));
    CHECK(directory.directory);
    /* A directory holds no file bytes, whatever a caller asks for. */
    CHECK(!fat_read(&directory, 0, 1, got));

    CHECK(!fat_open(&volume, "/boot/missing.cfg", &file));
    CHECK(!fat_open(&volume, "/nothere/deep.cfg", &file));
}

static void a_path_through_a_file_is_not_a_path(void) {
    struct fat_file file;
    format();
    write_file(ROOT_CLUSTER, "PLAIN   BIN", 32);
    CHECK(fat_mount(&device, &volume));

    CHECK(!fat_open(&volume, "/plain.bin/inside.txt", &file));
}

static void refuses_to_read_past_the_end_of_a_file(void) {
    static uint8_t got[64];
    format();
    write_file(ROOT_CLUSTER, "SHORT   BIN", 40);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/short.bin", &file));

    /* Zero-filling the tail would make a truncated kernel look like a whole
       one, so the read fails instead. */
    CHECK(!fat_read(&file, 0, 41, got));
    CHECK(!fat_read(&file, 40, 1, got));
    CHECK(!fat_read(&file, UINT64_MAX - 4, 8, got));
    CHECK(fat_read(&file, 39, 1, got));
}

static void a_chain_that_loops_does_not_hang(void) {
    static uint8_t got[4 * SECTOR_BYTES];
    format();
    uint32_t first = write_file(ROOT_CLUSTER, "LOOP    BIN", sizeof got);

    /* The last cluster points back at the first. A walk that trusts the chain
       to end never returns, and a bootloader that never returns says nothing. */
    set_fat(first + 3U, first);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/loop.bin", &file));
    CHECK(fat_read(&file, 0, sizeof got, got));

    /* Reading beyond what the loop covers must stop, not spin. The walk gives
       up after one cluster more than the volume holds. */
    static uint8_t scratch[IMAGE_BYTES];
    struct fat_file forged = file;
    forged.size = IMAGE_BYTES;
    CHECK(!fat_read(&forged, 0, IMAGE_BYTES, scratch));
}

static void a_cluster_outside_the_volume_is_refused(void) {
    static uint8_t got[32];
    format();
    write_file(ROOT_CLUSTER, "BAD     BIN", sizeof got);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/bad.bin", &file));

    file.first_cluster = volume.cluster_count + FAT_FIRST_DATA_CLUSTER + 10U;
    CHECK(!fat_read(&file, 0, sizeof got, got));

    file.first_cluster = 0;
    CHECK(!fat_read(&file, 0, sizeof got, got));
}

static void long_name_fragments_are_skipped(void) {
    format();
    /* A long-name entry sits immediately before the entry it names; a reader
       that does not skip it matches on fragments of a UTF-16 name. */
    append_entry(ROOT_CLUSTER, "\x41" "AAAAAAAAAA", FAT_ATTRIBUTE_LONG_NAME, 0, 0);
    write_file(ROOT_CLUSTER, "REAL    TXT", 16);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(fat_open(&volume, "/real.txt", &file));
    CHECK(file.size == 16);
}

static void the_volume_label_is_not_a_file(void) {
    format();
    append_entry(ROOT_CLUSTER, "TUNIXBOOT  ", FAT_ATTRIBUTE_VOLUME_LABEL, 0, 0);
    write_file(ROOT_CLUSTER, "AFTER   TXT", 8);
    CHECK(fat_mount(&device, &volume));

    struct fat_file file;
    CHECK(!fat_open(&volume, "/tunixboot", &file));
    CHECK(fat_open(&volume, "/after.txt", &file));
}

TEST_MAIN(
    mounts_a_volume();
    refuses_what_is_not_a_fat32_volume();
    a_volume_bigger_than_its_disk_is_refused();
    reads_a_file_in_the_root();
    matches_names_without_regard_to_case();
    reads_a_file_spanning_several_clusters();
    walks_into_subdirectories();
    a_path_through_a_file_is_not_a_path();
    refuses_to_read_past_the_end_of_a_file();
    a_chain_that_loops_does_not_hang();
    a_cluster_outside_the_volume_is_refused();
    long_name_fragments_are_skipped();
    the_volume_label_is_not_a_file();
)
