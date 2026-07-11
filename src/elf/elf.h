#ifndef TUNIX_BOOT_ELF_ELF_H
#define TUNIX_BOOT_ELF_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ELF64, enough of it to load a kernel and no more.
 *
 * This is the last place untrusted input is turned into addresses that get
 * written to, so it is the place where being wrong is most expensive: every
 * offset here came out of a file, and a segment that claims to live at an
 * address the loader believes is how a kernel image overwrites the loader that
 * is reading it. Parsing and loading are separate calls so the whole layout can
 * be checked before a single byte is placed.
 *
 * Reading is a callback rather than a filesystem call, so the same code loads
 * from FAT, from ext2, or from a buffer in a test.
 */

#define ELF_IDENT_BYTES 16U
#define ELF_MAGIC_BYTES 4U

#define ELF_IDENT_CLASS_OFFSET 4U
#define ELF_IDENT_DATA_OFFSET 5U
#define ELF_IDENT_VERSION_OFFSET 6U

#define ELF_CLASS_64 2U
#define ELF_DATA_LITTLE_ENDIAN 1U
#define ELF_VERSION_CURRENT 1U

#define ELF_HEADER_BYTES 64U
#define ELF_HEADER_TYPE_OFFSET 16U
#define ELF_HEADER_MACHINE_OFFSET 18U
#define ELF_HEADER_VERSION_OFFSET 20U
#define ELF_HEADER_ENTRY_OFFSET 24U
#define ELF_HEADER_PHOFF_OFFSET 32U
#define ELF_HEADER_PHENTSIZE_OFFSET 54U
#define ELF_HEADER_PHNUM_OFFSET 56U

#define ELF_TYPE_EXECUTABLE 2U
#define ELF_TYPE_SHARED 3U
#define ELF_MACHINE_X86_64 62U

#define ELF_PROGRAM_HEADER_BYTES 56U
#define ELF_PROGRAM_TYPE_OFFSET 0U
#define ELF_PROGRAM_FLAGS_OFFSET 4U
#define ELF_PROGRAM_OFFSET_OFFSET 8U
#define ELF_PROGRAM_VADDR_OFFSET 16U
#define ELF_PROGRAM_PADDR_OFFSET 24U
#define ELF_PROGRAM_FILESZ_OFFSET 32U
#define ELF_PROGRAM_MEMSZ_OFFSET 40U
#define ELF_PROGRAM_ALIGN_OFFSET 48U

#define ELF_PROGRAM_TYPE_LOAD 1U
#define ELF_PROGRAM_TYPE_DYNAMIC 2U
#define ELF_PROGRAM_TYPE_INTERPRETER 3U
#define ELF_PROGRAM_TYPE_TLS 7U

#define ELF_FLAG_EXECUTE 0x1U
#define ELF_FLAG_WRITE 0x2U
#define ELF_FLAG_READ 0x4U

/* More than any kernel has, and a bound on a count that comes from the file. */
#define ELF_MAX_SEGMENTS 16U
#define ELF_MAX_PROGRAM_HEADERS 64U

/* Copied out in pieces, so a segment larger than memory does not need a buffer
   the size of the segment. */
#define ELF_COPY_CHUNK_BYTES 4096U

typedef bool (*elf_read_fn)(void *context, uint64_t offset, size_t length,
                            void *out);

struct elf_segment {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_bytes;
    /* Never less than file_bytes. The difference is .bss and must be zeroed. */
    uint64_t memory_bytes;
    uint32_t flags;
};

struct elf_image {
    uint64_t entry;
    struct elf_segment segments[ELF_MAX_SEGMENTS];
    unsigned segment_count;
    /* The physical span every loadable segment falls inside. */
    uint64_t lowest_address;
    uint64_t highest_address;
};

struct elf_placement {
    /* Added to each segment's physical address before anything is written.
       Zero loads the kernel where it asked to be. */
    int64_t bias;
    /* Nothing is written outside this window, whatever the file claims. */
    uint64_t limit_low;
    uint64_t limit_high;
};

/* Validates the header and every program header, and records the loadable
   segments. Nothing is written to memory. */
bool elf_parse(elf_read_fn read, void *context, struct elf_image *out);

/* Places the segments and zeroes what the file does not cover. */
bool elf_load(elf_read_fn read, void *context, const struct elf_image *image,
              const struct elf_placement *placement);

/* Where the kernel starts, once placed. */
uint64_t elf_entry_point(const struct elf_image *image,
                         const struct elf_placement *placement);

#endif
