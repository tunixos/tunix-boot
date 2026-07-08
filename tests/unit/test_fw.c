#include "harness.h"
#include "fw/fw.h"

#define MIB (1024ULL * 1024ULL)

/* The whole point of the seam: a firmware that exists only here. */
struct mock_fw {
    struct fw_ops ops;
    unsigned snapshot_calls;
    unsigned read_calls;
    uint64_t last_lba;
    bool snapshot_result;
};

static bool mock_snapshot(const struct fw_ops *fw, struct memory_map *out) {
    struct mock_fw *self = (struct mock_fw *)(void *)(uintptr_t)fw;
    self->snapshot_calls++;
    if (!self->snapshot_result) return false;
    memory_map_add(out, MIB, 64 * MIB, MEMORY_KIND_USABLE);
    memory_map_finalize(out);
    return true;
}

static bool mock_read(const struct fw_ops *fw, uint64_t lba, uint32_t sectors,
                      void *out) {
    struct mock_fw *self = (struct mock_fw *)(void *)(uintptr_t)fw;
    self->read_calls++;
    self->last_lba = lba;
    (void)sectors;
    (void)out;
    return true;
}

static struct mock_fw make_mock(void) {
    struct mock_fw mock = {0};
    mock.ops.name = "mock";
    mock.ops.mem_snapshot = mock_snapshot;
    mock.ops.block_read = mock_read;
    mock.snapshot_result = true;
    return mock;
}

static void reaches_the_backend(void) {
    struct mock_fw mock = make_mock();
    struct memory_map map;
    uint8_t sector[512];

    CHECK(fw_mem_snapshot(&mock.ops, &map));
    CHECK(mock.snapshot_calls == 1);
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 64 * MIB);

    CHECK(fw_block_read(&mock.ops, 2048, 1, sector));
    CHECK(mock.read_calls == 1);
    CHECK(mock.last_lba == 2048);
    CHECK(fw_name(&mock.ops) != NULL);
}

static void an_unimplemented_operation_fails_rather_than_faulting(void) {
    struct fw_ops empty = {0};
    struct memory_map map;
    uint8_t sector[512];

    CHECK(!fw_mem_snapshot(&empty, &map));
    CHECK(!fw_block_read(&empty, 0, 1, sector));
    fw_console_write(&empty, "this must not call through a null pointer");
    CHECK(fw_name(&empty) != NULL);
}

static void no_firmware_at_all_fails_the_same_way(void) {
    struct memory_map map;
    uint8_t sector[512];

    CHECK(!fw_mem_snapshot(NULL, &map));
    CHECK(!fw_block_read(NULL, 0, 1, sector));
    fw_console_write(NULL, "nor this");
    CHECK(fw_name(NULL) != NULL);
}

static void nonsense_arguments_never_reach_the_backend(void) {
    struct mock_fw mock = make_mock();

    CHECK(!fw_block_read(&mock.ops, 0, 0, (void *)&mock));
    CHECK(!fw_block_read(&mock.ops, 0, 1, NULL));
    CHECK(mock.read_calls == 0);

    fw_console_write(&mock.ops, NULL);
}

static void the_map_is_cleared_before_the_backend_fills_it(void) {
    struct mock_fw mock = make_mock();
    struct memory_map map;

    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 4 * MIB, MEMORY_KIND_USABLE));

    /* A snapshot describes memory now, not memory plus whatever the caller
       happened to be holding. */
    CHECK(fw_mem_snapshot(&mock.ops, &map));
    CHECK(memory_map_total(&map, MEMORY_KIND_USABLE) == 64 * MIB);
    CHECK(memory_map_find(&map, 0) == NULL);
}

static void a_failed_snapshot_leaves_nothing_to_believe(void) {
    struct mock_fw mock = make_mock();
    struct memory_map map;
    mock.snapshot_result = false;

    memory_map_clear(&map);
    CHECK(memory_map_add(&map, 0, 4 * MIB, MEMORY_KIND_USABLE));

    CHECK(!fw_mem_snapshot(&mock.ops, &map));
    CHECK(map.count == 0);
}

TEST_MAIN(
    reaches_the_backend();
    an_unimplemented_operation_fails_rather_than_faulting();
    no_firmware_at_all_fails_the_same_way();
    nonsense_arguments_never_reach_the_backend();
    the_map_is_cleared_before_the_backend_fills_it();
    a_failed_snapshot_leaves_nothing_to_believe();
)
