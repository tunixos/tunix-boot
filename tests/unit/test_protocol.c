#include "harness.h"
#include "protocol/protocol.h"
#include "util/mem.h"
#include "util/str.h"

#define IMAGE_BYTES 8192U
#define ARENA_BYTES 16384U
#define MIB (1024ULL * 1024ULL)

/* A kernel image with requests in it, laid out as a compiler would: the request
   structures sit among other data at eight-byte alignment. */
static uint8_t image[IMAGE_BYTES] __attribute__((aligned(16)));
static uint8_t arena_backing[ARENA_BYTES] __attribute__((aligned(16)));
static struct arena arena;
static struct memory_map memory;
static struct boot_facts facts;

static struct boot_request *place_request(uint64_t offset, uint64_t id,
                                          uint64_t revision) {
    struct boot_request *request = (struct boot_request *)(void *)(image + offset);
    request->magic[0] = BOOT_REQUEST_MAGIC_LOW;
    request->magic[1] = BOOT_REQUEST_MAGIC_HIGH;
    request->id = id;
    request->revision = revision;
    request->response = NULL;
    return request;
}

static void setup(void) {
    memset(image, 0, sizeof image);
    arena_init(&arena, arena_backing, sizeof arena_backing);

    memory_map_clear(&memory);
    CHECK(memory_map_add(&memory, 0, MIB, MEMORY_KIND_USABLE));
    CHECK(memory_map_add(&memory, 2 * MIB, MIB, MEMORY_KIND_RESERVED));
    CHECK(memory_map_add(&memory, 4 * MIB, 8 * MIB, MEMORY_KIND_USABLE));
    memory_map_finalize(&memory);

    facts.memory = &memory;
    facts.kernel_physical_base = 0x400000;
    facts.kernel_virtual_base = 0xFFFFFFFF80000000ULL;
    facts.command_line = "root=/dev/sda1 quiet";
}

static int answer_all(void) {
    return protocol_answer(image, sizeof image, &facts, &arena);
}

static void answers_a_memory_map_request(void) {
    setup();
    struct boot_request *request = place_request(512, BOOT_REQUEST_MEMORY_MAP, 0);
    CHECK(answer_all() == 1);

    struct boot_memory_map_response *response = request->response;
    CHECK(response != NULL);
    CHECK(response->entry_count == memory.count);
    CHECK(response->entries != NULL);

    CHECK(response->entries[0].base == 0);
    CHECK(response->entries[0].length == MIB);
    CHECK(response->entries[0].kind == BOOT_MEMORY_USABLE);
    CHECK(response->entries[1].kind == BOOT_MEMORY_RESERVED);
    CHECK(response->entries[2].base == 4 * MIB);
}

static void answers_a_kernel_address_request(void) {
    setup();
    struct boot_request *request =
        place_request(1024, BOOT_REQUEST_KERNEL_ADDRESS, 0);
    CHECK(answer_all() == 1);

    struct boot_kernel_address_response *response = request->response;
    CHECK(response != NULL);
    /* Both halves: a kernel in the higher half needs the pair to convert
       between them, and either one alone is useless. */
    CHECK(response->physical_base == 0x400000);
    CHECK(response->virtual_base == 0xFFFFFFFF80000000ULL);
}

static void answers_a_command_line_request(void) {
    setup();
    struct boot_request *request =
        place_request(256, BOOT_REQUEST_COMMAND_LINE, 0);
    CHECK(answer_all() == 1);

    struct boot_command_line_response *response = request->response;
    CHECK(response != NULL);
    CHECK(str_equal_cstr(str_from_cstr(response->command_line),
                         "root=/dev/sda1 quiet"));
}

static void a_kernel_with_no_command_line_gets_an_empty_one(void) {
    setup();
    facts.command_line = NULL;
    struct boot_request *request =
        place_request(256, BOOT_REQUEST_COMMAND_LINE, 0);
    CHECK(answer_all() == 1);

    struct boot_command_line_response *response = request->response;
    CHECK(response != NULL);
    /* Never null, so the kernel has one case to handle rather than two. */
    CHECK(response->command_line != NULL);
    CHECK(response->command_line[0] == '\0');
}

static void answers_a_loader_info_request(void) {
    setup();
    struct boot_request *request = place_request(64, BOOT_REQUEST_LOADER_INFO, 0);
    CHECK(answer_all() == 1);

    struct boot_loader_info_response *response = request->response;
    CHECK(response != NULL);
    CHECK(response->name != NULL && response->name[0] != '\0');
    CHECK(response->version != NULL && response->version[0] != '\0');
}

static void answers_every_request_in_the_image(void) {
    setup();
    struct boot_request *first = place_request(128, BOOT_REQUEST_MEMORY_MAP, 0);
    struct boot_request *second =
        place_request(2048, BOOT_REQUEST_COMMAND_LINE, 0);
    struct boot_request *third =
        place_request(6000 & ~7U, BOOT_REQUEST_KERNEL_ADDRESS, 0);

    CHECK(answer_all() == 3);
    CHECK(first->response != NULL);
    CHECK(second->response != NULL);
    CHECK(third->response != NULL);
}

static void an_unknown_request_keeps_its_null_response(void) {
    setup();
    struct boot_request *unknown = place_request(512, 9999, 0);
    struct boot_request *known = place_request(1024, BOOT_REQUEST_LOADER_INFO, 0);

    /* Counted as handled — the loader did decide about it — but answered with
       nothing, which is how the protocol says "I have never heard of this". */
    CHECK(answer_all() == 2);
    CHECK(unknown->response == NULL);
    CHECK(known->response != NULL);
}

static void a_kernel_asking_for_a_later_revision_is_told_what_it_got(void) {
    setup();
    struct boot_request *request =
        place_request(512, BOOT_REQUEST_KERNEL_ADDRESS, 99);
    CHECK(answer_all() == 1);

    struct boot_kernel_address_response *response = request->response;
    CHECK(response != NULL);
    /* Answered at the revision this loader knows, not the one asked for. */
    CHECK(response->revision == BOOT_PROTOCOL_REVISION);
}

static void half_a_magic_is_not_a_request(void) {
    setup();
    /* One half of the pair will turn up in real data eventually; both halves
       adjacent will not. */
    uint64_t *at = (uint64_t *)(void *)(image + 512);
    at[0] = BOOT_REQUEST_MAGIC_LOW;
    at[1] = 0x1234567890ABCDEFULL;

    uint64_t *other = (uint64_t *)(void *)(image + 1024);
    other[0] = 0;
    other[1] = BOOT_REQUEST_MAGIC_HIGH;

    CHECK(answer_all() == 0);
}

static void an_image_with_no_requests_is_not_a_failure(void) {
    setup();
    CHECK(answer_all() == 0);
}

static void a_request_at_the_very_end_is_found(void) {
    setup();
    uint64_t offset = (IMAGE_BYTES - sizeof(struct boot_request)) & ~7ULL;
    struct boot_request *request =
        place_request(offset, BOOT_REQUEST_LOADER_INFO, 0);
    CHECK(answer_all() == 1);
    CHECK(request->response != NULL);
}

static void a_request_running_off_the_end_is_not_read(void) {
    setup();
    /* Only the magic fits; the id and response pointer are past the image. A
       scan that reads them is reading whatever follows the kernel in memory. */
    uint64_t *at = (uint64_t *)(void *)(image + IMAGE_BYTES - 16U);
    at[0] = BOOT_REQUEST_MAGIC_LOW;
    at[1] = BOOT_REQUEST_MAGIC_HIGH;

    CHECK(answer_all() == 0);
}

static void an_image_smaller_than_a_request_is_scanned_safely(void) {
    setup();
    CHECK(protocol_answer(image, 8, &facts, &arena) == 0);
    CHECK(protocol_answer(image, 0, &facts, &arena) == 0);
}

static void running_out_of_arena_fails_rather_than_half_answers(void) {
    static uint8_t tiny_backing[64] __attribute__((aligned(16)));
    struct arena tiny;
    setup();
    arena_init(&tiny, tiny_backing, sizeof tiny_backing);

    place_request(128, BOOT_REQUEST_MEMORY_MAP, 0);
    place_request(1024, BOOT_REQUEST_LOADER_INFO, 0);

    /* A kernel that got half its answers cannot be trusted to start, so this
       reports failure rather than a count. */
    CHECK(protocol_answer(image, sizeof image, &facts, &tiny) == -1);
}

static void refuses_nonsense_arguments(void) {
    setup();
    CHECK(protocol_answer(NULL, IMAGE_BYTES, &facts, &arena) == -1);
    CHECK(protocol_answer(image, IMAGE_BYTES, NULL, &arena) == -1);
    CHECK(protocol_answer(image, IMAGE_BYTES, &facts, NULL) == -1);
}

static void every_memory_kind_reaches_the_kernel_as_something(void) {
    /* A kind the protocol has no name for must arrive as reserved: memory the
       kernel does not understand is memory it must not use. */
    CHECK(protocol_memory_kind(MEMORY_KIND_USABLE) == BOOT_MEMORY_USABLE);
    CHECK(protocol_memory_kind(MEMORY_KIND_BAD) == BOOT_MEMORY_BAD);
    CHECK(protocol_memory_kind(MEMORY_KIND_ACPI_NVS) == BOOT_MEMORY_ACPI_NVS);
    CHECK(protocol_memory_kind((enum memory_kind)(MEMORY_KIND_BAD + 1)) ==
          BOOT_MEMORY_RESERVED);
}

TEST_MAIN(
    answers_a_memory_map_request();
    answers_a_kernel_address_request();
    answers_a_command_line_request();
    a_kernel_with_no_command_line_gets_an_empty_one();
    answers_a_loader_info_request();
    answers_every_request_in_the_image();
    an_unknown_request_keeps_its_null_response();
    a_kernel_asking_for_a_later_revision_is_told_what_it_got();
    half_a_magic_is_not_a_request();
    an_image_with_no_requests_is_not_a_failure();
    a_request_at_the_very_end_is_found();
    a_request_running_off_the_end_is_not_read();
    an_image_smaller_than_a_request_is_scanned_safely();
    running_out_of_arena_fails_rather_than_half_answers();
    refuses_nonsense_arguments();
    every_memory_kind_reaches_the_kernel_as_something();
)
