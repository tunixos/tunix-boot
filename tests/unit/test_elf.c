#include "harness.h"
#include "elf/elf.h"
#include "util/mem.h"

#define FILE_BYTES 32768U
#define LOAD_BASE 0x200000ULL
#define ARENA_BYTES 65536U

/* An ELF64 file assembled byte by byte, so the tests can produce the files a
   linker never would: a segment claiming more file bytes than memory bytes, two
   segments over the same address, a program header count that runs off the end. */
static uint8_t file[FILE_BYTES];
static unsigned program_headers;

/* Where segments are placed. Physical addresses in the file are far from any
   real address here, so the bias is what redirects them into this buffer. */
static uint8_t arena[ARENA_BYTES];

static struct elf_image image;

static bool file_read(void *context, uint64_t offset, size_t length, void *out) {
    (void)context;
    if (offset + length > FILE_BYTES) return false;
    memcpy(out, file + offset, length);
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

static void put_u64(uint8_t *at, uint64_t value) {
    for (unsigned index = 0; index < 8; index++)
        at[index] = (uint8_t)(value >> (index * 8));
}

static uint8_t *program_header(unsigned index) {
    return file + ELF_HEADER_BYTES + index * ELF_PROGRAM_HEADER_BYTES;
}

static void begin_file(void) {
    memset(file, 0, sizeof file);
    memset(arena, 0xCC, sizeof arena);
    program_headers = 0;

    file[0] = 0x7F;
    file[1] = 'E';
    file[2] = 'L';
    file[3] = 'F';
    file[ELF_IDENT_CLASS_OFFSET] = ELF_CLASS_64;
    file[ELF_IDENT_DATA_OFFSET] = ELF_DATA_LITTLE_ENDIAN;
    file[ELF_IDENT_VERSION_OFFSET] = ELF_VERSION_CURRENT;

    put_u16(file + ELF_HEADER_TYPE_OFFSET, ELF_TYPE_EXECUTABLE);
    put_u16(file + ELF_HEADER_MACHINE_OFFSET, ELF_MACHINE_X86_64);
    put_u32(file + ELF_HEADER_VERSION_OFFSET, ELF_VERSION_CURRENT);
    put_u64(file + ELF_HEADER_ENTRY_OFFSET, LOAD_BASE);
    put_u64(file + ELF_HEADER_PHOFF_OFFSET, ELF_HEADER_BYTES);
    put_u16(file + ELF_HEADER_PHENTSIZE_OFFSET, ELF_PROGRAM_HEADER_BYTES);
    put_u16(file + ELF_HEADER_PHNUM_OFFSET, 0);
}

static uint8_t *add_program_header(uint32_t type, uint64_t offset,
                                   uint64_t address, uint64_t file_bytes,
                                   uint64_t memory_bytes, uint32_t flags) {
    uint8_t *at = program_header(program_headers++);
    put_u32(at + ELF_PROGRAM_TYPE_OFFSET, type);
    put_u32(at + ELF_PROGRAM_FLAGS_OFFSET, flags);
    put_u64(at + ELF_PROGRAM_OFFSET_OFFSET, offset);
    put_u64(at + ELF_PROGRAM_VADDR_OFFSET, address);
    put_u64(at + ELF_PROGRAM_PADDR_OFFSET, address);
    put_u64(at + ELF_PROGRAM_FILESZ_OFFSET, file_bytes);
    put_u64(at + ELF_PROGRAM_MEMSZ_OFFSET, memory_bytes);
    put_u64(at + ELF_PROGRAM_ALIGN_OFFSET, 4096);
    put_u16(file + ELF_HEADER_PHNUM_OFFSET, (uint16_t)program_headers);
    return at;
}

static uint8_t content_byte(size_t index) {
    return (uint8_t)(index * 41U + (index >> 6) * 7U + 3U);
}

/* Segment content laid at `offset`, distinct per position so a copy that lands
   in the wrong place cannot look right. */
static void fill_content(uint64_t offset, size_t length) {
    for (size_t index = 0; index < length; index++)
        file[offset + index] = content_byte(index);
}

static struct elf_placement placement_into_arena(void) {
    struct elf_placement placement;
    placement.bias = (int64_t)((uint64_t)(uintptr_t)arena - LOAD_BASE);
    placement.limit_low = (uint64_t)(uintptr_t)arena;
    placement.limit_high = (uint64_t)(uintptr_t)arena + ARENA_BYTES;
    return placement;
}

static void parses_a_plain_executable(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 256, 512,
                       ELF_FLAG_READ | ELF_FLAG_EXECUTE);
    CHECK(elf_parse(file_read, NULL, &image));

    CHECK(image.entry == LOAD_BASE);
    CHECK(image.segment_count == 1);
    CHECK(image.segments[0].file_bytes == 256);
    CHECK(image.segments[0].memory_bytes == 512);
    CHECK(image.lowest_address == LOAD_BASE);
    CHECK(image.highest_address == LOAD_BASE + 512);
}

static void refuses_what_is_not_an_elf64_it_can_run(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    file[1] = 'X';
    CHECK(!elf_parse(file_read, NULL, &image));

    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    file[ELF_IDENT_CLASS_OFFSET] = 1;
    CHECK(!elf_parse(file_read, NULL, &image));

    /* A big-endian file would have every field read back byte-reversed. */
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    file[ELF_IDENT_DATA_OFFSET] = 2;
    CHECK(!elf_parse(file_read, NULL, &image));

    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    put_u16(file + ELF_HEADER_MACHINE_OFFSET, 3);
    CHECK(!elf_parse(file_read, NULL, &image));

    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    put_u16(file + ELF_HEADER_TYPE_OFFSET, 1);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_a_program_header_of_the_wrong_size(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    /* Walking with the wrong stride reads each field out of the middle of
       another one, and every value that comes back is plausible. */
    put_u16(file + ELF_HEADER_PHENTSIZE_OFFSET, 32);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_a_header_count_that_runs_off_the_file(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    put_u16(file + ELF_HEADER_PHNUM_OFFSET, ELF_MAX_PROGRAM_HEADERS + 1U);
    CHECK(!elf_parse(file_read, NULL, &image));

    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 16, 16, 0);
    put_u64(file + ELF_HEADER_PHOFF_OFFSET, FILE_BYTES - 8U);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_a_segment_larger_in_the_file_than_in_memory(void) {
    begin_file();
    /* The copy would run past the end of what was reserved for it. */
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 512, 256, 0);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_sizes_that_wrap(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, UINT64_MAX - 16U, 8,
                       1024, 0);
    CHECK(!elf_parse(file_read, NULL, &image));

    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, UINT64_MAX - 4U, LOAD_BASE, 64,
                       64, 0);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_overlapping_segments(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 256, 4096, 0);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 7936, LOAD_BASE + 2048U,
                       128, 1024, 0);
    /* Which one survives would depend on the order they appear in the file. */
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void refuses_a_file_wanting_an_interpreter(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_INTERPRETER, 4096, 0, 16, 16, 0);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 256, 256, 0);
    /* There is no dynamic linker here, and half-linked is worse than refused. */
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void ignores_segments_it_does_not_load(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 256, 256, 0);
    add_program_header(ELF_PROGRAM_TYPE_DYNAMIC, 4096, 0, 16, 16, 0);
    add_program_header(ELF_PROGRAM_TYPE_TLS, 4096, 0, 16, 16, 0);

    CHECK(elf_parse(file_read, NULL, &image));
    CHECK(image.segment_count == 1);
}

static void a_file_with_nothing_to_load_is_refused(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_DYNAMIC, 4096, 0, 16, 16, 0);
    CHECK(!elf_parse(file_read, NULL, &image));
}

static void loads_a_segment_where_the_bias_puts_it(void) {
    begin_file();
    fill_content(4096, 256);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 256, 512,
                       ELF_FLAG_READ | ELF_FLAG_WRITE);
    CHECK(elf_parse(file_read, NULL, &image));

    struct elf_placement placement = placement_into_arena();
    CHECK(elf_load(file_read, NULL, &image, &placement));

    for (size_t index = 0; index < 256; index++)
        CHECK(arena[index] == content_byte(index));
    /* The bytes the file does not cover are .bss and must arrive zeroed. */
    for (size_t index = 256; index < 512; index++) CHECK(arena[index] == 0);
    /* And nothing beyond the segment was touched. */
    CHECK(arena[512] == 0xCC);

    CHECK(elf_entry_point(&image, &placement) == (uint64_t)(uintptr_t)arena);
}

static void loads_several_segments(void) {
    begin_file();
    fill_content(4096, 128);
    fill_content(6144, 64);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 128, 128,
                       ELF_FLAG_READ | ELF_FLAG_EXECUTE);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 6144, LOAD_BASE + 4096U, 64, 256,
                       ELF_FLAG_READ | ELF_FLAG_WRITE);
    CHECK(elf_parse(file_read, NULL, &image));
    CHECK(image.segment_count == 2);

    struct elf_placement placement = placement_into_arena();
    CHECK(elf_load(file_read, NULL, &image, &placement));

    for (size_t index = 0; index < 128; index++)
        CHECK(arena[index] == content_byte(index));
    for (size_t index = 0; index < 64; index++)
        CHECK(arena[4096 + index] == content_byte(index));
    for (size_t index = 64; index < 256; index++)
        CHECK(arena[4096 + index] == 0);
    /* The gap between them belongs to neither and stays as it was. */
    CHECK(arena[2048] == 0xCC);
}

static void a_segment_outside_the_window_moves_nothing(void) {
    begin_file();
    fill_content(4096, 128);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 128, 128, 0);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE + ARENA_BYTES,
                       128, 128, 0);
    CHECK(elf_parse(file_read, NULL, &image));

    struct elf_placement placement = placement_into_arena();
    CHECK(!elf_load(file_read, NULL, &image, &placement));

    /* The first segment would have fitted. Writing it and then failing would
       leave memory half-overwritten by a file that was rejected. */
    for (size_t index = 0; index < 128; index++) CHECK(arena[index] == 0xCC);
}

static void a_narrow_window_refuses_a_segment_that_would_run_past_it(void) {
    begin_file();
    fill_content(4096, 128);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 128, 1024, 0);
    CHECK(elf_parse(file_read, NULL, &image));

    struct elf_placement placement = placement_into_arena();
    placement.limit_high = placement.limit_low + 512U;
    CHECK(!elf_load(file_read, NULL, &image, &placement));
    CHECK(arena[0] == 0xCC);

    /* Room for the whole memory size, not just the part the file fills. */
    placement.limit_high = placement.limit_low + 1024U;
    CHECK(elf_load(file_read, NULL, &image, &placement));
}

static void an_unreadable_file_is_a_failed_load(void) {
    begin_file();
    add_program_header(ELF_PROGRAM_TYPE_LOAD, FILE_BYTES - 64U, LOAD_BASE, 256,
                       256, 0);
    CHECK(elf_parse(file_read, NULL, &image));

    /* The header is consistent; the bytes are simply not there. */
    struct elf_placement placement = placement_into_arena();
    CHECK(!elf_load(file_read, NULL, &image, &placement));
}

static void a_segment_larger_than_the_copy_buffer(void) {
    static const uint64_t big = 3U * ELF_COPY_CHUNK_BYTES + 137U;
    begin_file();
    fill_content(1024, (size_t)big);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 1024, LOAD_BASE, big, big + 500U, 0);
    CHECK(elf_parse(file_read, NULL, &image));

    struct elf_placement placement = placement_into_arena();
    CHECK(elf_load(file_read, NULL, &image, &placement));

    /* Chunked copying is where an off-by-one repeats or drops a chunk. */
    for (size_t index = 0; index < big; index++)
        CHECK(arena[index] == content_byte(index));
    for (size_t index = 0; index < 500; index++)
        CHECK(arena[big + index] == 0);
}

static void a_negative_bias_loads_below_the_stated_address(void) {
    begin_file();
    fill_content(4096, 64);
    add_program_header(ELF_PROGRAM_TYPE_LOAD, 4096, LOAD_BASE, 64, 64, 0);
    CHECK(elf_parse(file_read, NULL, &image));

    struct elf_placement placement = placement_into_arena();
    CHECK(placement.bias < 0 || placement.bias > 0);
    CHECK(elf_load(file_read, NULL, &image, &placement));
    for (size_t index = 0; index < 64; index++)
        CHECK(arena[index] == content_byte(index));

    /* A bias that would take an address below zero is not a placement. */
    struct elf_placement impossible = placement;
    impossible.bias = -(int64_t)(LOAD_BASE + 4096U);
    impossible.limit_low = 0;
    CHECK(!elf_load(file_read, NULL, &image, &impossible));
}

TEST_MAIN(
    parses_a_plain_executable();
    refuses_what_is_not_an_elf64_it_can_run();
    refuses_a_program_header_of_the_wrong_size();
    refuses_a_header_count_that_runs_off_the_file();
    refuses_a_segment_larger_in_the_file_than_in_memory();
    refuses_sizes_that_wrap();
    refuses_overlapping_segments();
    refuses_a_file_wanting_an_interpreter();
    ignores_segments_it_does_not_load();
    a_file_with_nothing_to_load_is_refused();
    loads_a_segment_where_the_bias_puts_it();
    loads_several_segments();
    a_segment_outside_the_window_moves_nothing();
    a_narrow_window_refuses_a_segment_that_would_run_past_it();
    an_unreadable_file_is_a_failed_load();
    a_segment_larger_than_the_copy_buffer();
    a_negative_bias_loads_below_the_stated_address();
)
