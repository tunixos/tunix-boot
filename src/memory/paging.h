#ifndef TUNIX_BOOT_MEMORY_PAGING_H
#define TUNIX_BOOT_MEMORY_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/arena.h"

/*
 * Building the page tables the kernel will run on.
 *
 * stage2 brought up a 4 GiB identity map so the loader could reach memory at
 * all. That map is the loader's, not the kernel's: a kernel linked into the
 * higher half needs its own tables, mapped where it expects to be, before it is
 * entered. This builds them.
 *
 * Tables come from an arena, so there is no allocator to fail halfway and
 * nothing to free. Physical addresses are written into the entries and used
 * directly as pointers, which holds because the loader runs identity mapped —
 * and holds in the tests, where the arena is host memory and the addresses in
 * the entries are host pointers.
 */

#define PAGE_BYTES 4096ULL
#define PAGE_LARGE_BYTES (2ULL * 1024ULL * 1024ULL)
#define PAGE_HUGE_BYTES (1024ULL * 1024ULL * 1024ULL)

#define PAGE_TABLE_ENTRIES 512U
#define PAGE_TABLE_BYTES (PAGE_TABLE_ENTRIES * 8U)

#define PAGE_LEVEL_TABLE 1U
#define PAGE_LEVEL_DIRECTORY 2U
#define PAGE_LEVEL_POINTER 3U
#define PAGE_LEVEL_ROOT 4U

#define PAGE_SHIFT 12U
#define PAGE_INDEX_BITS 9U
#define PAGE_INDEX_MASK 0x1FFULL

/* Bits 12..51 of an entry are the address; the rest are flags. */
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_NO_CACHE (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY (1ULL << 6)
#define PAGE_SIZE_LARGE (1ULL << 7)
#define PAGE_GLOBAL (1ULL << 8)
#define PAGE_NO_EXECUTE (1ULL << 63)

/* The flags an intermediate entry carries. Permission is refused at the level
   that grants it, so intermediates are permissive and the leaf decides. */
#define PAGE_INTERMEDIATE_FLAGS (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)

/* x86-64 requires bits 48..63 to copy bit 47. An address that does not is one
   the processor will fault on rather than translate. */
#define PAGE_CANONICAL_BIT 47U

struct page_tables {
    struct arena *arena;
    /* Physical address of the PML4, which is what goes in CR3. */
    uint64_t root;
    /* Set when the processor was found to support gigabyte pages. */
    bool huge_pages;
};

bool paging_create(struct page_tables *tables, struct arena *arena,
                   bool huge_pages);

/* Maps `bytes` from `virtual` to `physical`. Both must be page aligned, as must
   the length. Uses the largest page size that fits and stays aligned.

   Remapping an address that is already mapped is refused: it is either a bug in
   whoever asked, or a kernel image whose segments overlap, and quietly changing
   a live translation is the worst of the three outcomes. */
bool paging_map(struct page_tables *tables, uint64_t virtual, uint64_t physical,
                uint64_t bytes, uint64_t flags);

/* Walks the tables that were built. The translation the processor would do. */
bool paging_translate(const struct page_tables *tables, uint64_t virtual,
                      uint64_t *physical, uint64_t *flags);

bool paging_address_is_canonical(uint64_t address);

/* Installs the tables. Everything the loader still needs must be mapped in them
   before this is called, including the code making the call. */
void paging_activate(const struct page_tables *tables);

#endif
