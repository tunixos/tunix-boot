#include "elf/elf.h"
#include "util/checked.h"
#include "util/mem.h"
#include "util/reader.h"

static const uint8_t elf_magic[ELF_MAGIC_BYTES] = {0x7F, 'E', 'L', 'F'};

static bool biased(uint64_t address, int64_t bias, uint64_t *out) {
    if (bias >= 0) return checked_add_u64(address, (uint64_t)bias, out);

    uint64_t magnitude = (uint64_t)(-(bias + 1)) + 1U;
    if (magnitude > address) return false;
    *out = address - magnitude;
    return true;
}

static bool segment_within(const struct elf_segment *segment,
                           const struct elf_placement *placement) {
    uint64_t start, end;
    if (!biased(segment->physical_address, placement->bias, &start)) return false;
    if (!checked_add_u64(start, segment->memory_bytes, &end)) return false;
    if (start < placement->limit_low || end > placement->limit_high) return false;
    return true;
}

static bool segments_overlap(const struct elf_segment *left,
                             const struct elf_segment *right) {
    uint64_t left_end, right_end;
    if (!checked_add_u64(left->physical_address, left->memory_bytes, &left_end))
        return true;
    if (!checked_add_u64(right->physical_address, right->memory_bytes, &right_end))
        return true;
    return left->physical_address < right_end &&
           right->physical_address < left_end;
}

static bool parse_header(elf_read_fn read, void *context, uint64_t *program_offset,
                         uint16_t *program_count, uint64_t *entry) {
    uint8_t header[ELF_HEADER_BYTES];
    if (!read(context, 0, sizeof header, header)) return false;

    if (memcmp(header, elf_magic, ELF_MAGIC_BYTES) != 0) return false;
    if (header[ELF_IDENT_CLASS_OFFSET] != ELF_CLASS_64) return false;
    if (header[ELF_IDENT_DATA_OFFSET] != ELF_DATA_LITTLE_ENDIAN) return false;
    if (header[ELF_IDENT_VERSION_OFFSET] != ELF_VERSION_CURRENT) return false;

    struct reader reader;
    reader_init(&reader, header, sizeof header);

    uint16_t type, machine, entry_size, count;
    uint32_t version;
    if (!reader_seek(&reader, ELF_HEADER_TYPE_OFFSET)) return false;
    if (!reader_u16(&reader, &type)) return false;
    if (!reader_u16(&reader, &machine)) return false;
    if (!reader_u32(&reader, &version)) return false;
    if (!reader_u64(&reader, entry)) return false;
    if (!reader_seek(&reader, ELF_HEADER_PHOFF_OFFSET)) return false;
    if (!reader_u64(&reader, program_offset)) return false;
    if (!reader_seek(&reader, ELF_HEADER_PHENTSIZE_OFFSET)) return false;
    if (!reader_u16(&reader, &entry_size)) return false;
    if (!reader_u16(&reader, &count)) return false;

    if (type != ELF_TYPE_EXECUTABLE && type != ELF_TYPE_SHARED) return false;
    if (machine != ELF_MACHINE_X86_64) return false;
    if (version != ELF_VERSION_CURRENT) return false;

    /* A program header of another size is a file this code would walk with the
       wrong stride, reading fields out of the middle of other fields. */
    if (entry_size != ELF_PROGRAM_HEADER_BYTES) return false;
    if (count == 0 || count > ELF_MAX_PROGRAM_HEADERS) return false;

    *program_count = count;
    return true;
}

static bool parse_program_header(const uint8_t *raw, struct elf_segment *out,
                                 uint32_t *type) {
    struct reader reader;
    reader_init(&reader, raw, ELF_PROGRAM_HEADER_BYTES);

    uint64_t align;
    if (!reader_u32(&reader, type)) return false;
    if (!reader_u32(&reader, &out->flags)) return false;
    if (!reader_u64(&reader, &out->file_offset)) return false;
    if (!reader_u64(&reader, &out->virtual_address)) return false;
    if (!reader_u64(&reader, &out->physical_address)) return false;
    if (!reader_u64(&reader, &out->file_bytes)) return false;
    if (!reader_u64(&reader, &out->memory_bytes)) return false;
    if (!reader_u64(&reader, &align)) return false;

    if (*type != ELF_PROGRAM_TYPE_LOAD) return true;

    /* More bytes in the file than in memory means the copy would run past the
       end of the segment it is filling. */
    if (out->file_bytes > out->memory_bytes) return false;
    if (out->memory_bytes == 0) return false;
    if (align != 0 && (align & (align - 1U)) != 0) return false;

    uint64_t unused;
    if (!checked_add_u64(out->file_offset, out->file_bytes, &unused)) return false;
    if (!checked_add_u64(out->physical_address, out->memory_bytes, &unused))
        return false;
    if (!checked_add_u64(out->virtual_address, out->memory_bytes, &unused))
        return false;
    return true;
}

bool elf_parse(elf_read_fn read, void *context, struct elf_image *out) {
    if (!read || !out) return false;

    out->segment_count = 0;
    out->lowest_address = UINT64_MAX;
    out->highest_address = 0;

    uint64_t program_offset;
    uint16_t program_count;
    if (!parse_header(read, context, &program_offset, &program_count, &out->entry))
        return false;

    for (uint16_t index = 0; index < program_count; index++) {
        uint64_t at;
        if (!checked_mul_u64(index, ELF_PROGRAM_HEADER_BYTES, &at)) return false;
        if (!checked_add_u64(program_offset, at, &at)) return false;

        uint8_t raw[ELF_PROGRAM_HEADER_BYTES];
        if (!read(context, at, sizeof raw, raw)) return false;

        struct elf_segment segment;
        uint32_t type;
        if (!parse_program_header(raw, &segment, &type)) return false;

        /* A kernel that wants an interpreter wants a dynamic linker, and there
           is not one here. Refusing beats loading it half-linked. */
        if (type == ELF_PROGRAM_TYPE_INTERPRETER) return false;
        if (type != ELF_PROGRAM_TYPE_LOAD) continue;

        if (out->segment_count == ELF_MAX_SEGMENTS) return false;

        /* Overlapping segments would have one write over another, and which one
           survives depends on the order they appear in the file. */
        for (unsigned other = 0; other < out->segment_count; other++) {
            if (segments_overlap(&out->segments[other], &segment)) return false;
        }

        uint64_t end = segment.physical_address + segment.memory_bytes;
        if (segment.physical_address < out->lowest_address)
            out->lowest_address = segment.physical_address;
        if (end > out->highest_address) out->highest_address = end;

        out->segments[out->segment_count++] = segment;
    }

    if (out->segment_count == 0) return false;
    return true;
}

static bool zero_range(uint64_t address, uint64_t length) {
    uint8_t *cursor = (uint8_t *)(uintptr_t)address;
    while (length > 0) {
        size_t chunk = length > ELF_COPY_CHUNK_BYTES ? ELF_COPY_CHUNK_BYTES
                                                     : (size_t)length;
        memset(cursor, 0, chunk);
        cursor += chunk;
        length -= chunk;
    }
    return true;
}

static bool copy_segment(elf_read_fn read, void *context,
                         const struct elf_segment *segment, uint64_t destination) {
    uint64_t remaining = segment->file_bytes;
    uint64_t from = segment->file_offset;
    uint8_t *cursor = (uint8_t *)(uintptr_t)destination;

    while (remaining > 0) {
        size_t chunk = remaining > ELF_COPY_CHUNK_BYTES ? ELF_COPY_CHUNK_BYTES
                                                        : (size_t)remaining;
        if (!read(context, from, chunk, cursor)) return false;
        cursor += chunk;
        from += chunk;
        remaining -= chunk;
    }

    /* Whatever the file does not cover is .bss, and a kernel is entitled to
       find it zeroed. */
    return zero_range(destination + segment->file_bytes,
                      segment->memory_bytes - segment->file_bytes);
}

bool elf_load(elf_read_fn read, void *context, const struct elf_image *image,
              const struct elf_placement *placement) {
    if (!read || !image || !placement) return false;
    if (placement->limit_low >= placement->limit_high) return false;
    if (image->segment_count == 0) return false;

    /* Every segment is checked against the window before any of them is
       written, so a file that would land outside it moves nothing at all. */
    for (unsigned index = 0; index < image->segment_count; index++) {
        if (!segment_within(&image->segments[index], placement)) return false;
    }

    for (unsigned index = 0; index < image->segment_count; index++) {
        const struct elf_segment *segment = &image->segments[index];
        uint64_t destination;
        if (!biased(segment->physical_address, placement->bias, &destination))
            return false;
        if (!copy_segment(read, context, segment, destination)) return false;
    }
    return true;
}

uint64_t elf_entry_point(const struct elf_image *image,
                         const struct elf_placement *placement) {
    uint64_t entry;
    if (!biased(image->entry, placement->bias, &entry)) return 0;
    return entry;
}
