#include "cpu/control.h"
#include "memory/paging.h"
#include "util/checked.h"
#include "util/mem.h"

static uint64_t *table_of(uint64_t physical) {
    return (uint64_t *)(uintptr_t)physical;
}

static unsigned index_at(uint64_t virtual, unsigned level) {
    unsigned shift = PAGE_SHIFT + PAGE_INDEX_BITS * (level - 1U);
    return (unsigned)((virtual >> shift) & PAGE_INDEX_MASK);
}

static uint64_t level_span(unsigned level) {
    return 1ULL << (PAGE_SHIFT + PAGE_INDEX_BITS * (level - 1U));
}

bool paging_address_is_canonical(uint64_t address) {
    uint64_t sign = (address >> PAGE_CANONICAL_BIT) & 1ULL;
    uint64_t upper = address >> (PAGE_CANONICAL_BIT + 1U);
    return sign == 0 ? upper == 0 : upper == (UINT64_MAX >> (PAGE_CANONICAL_BIT + 1U));
}

static uint64_t *allocate_table(struct page_tables *tables) {
    void *memory = arena_allocate_aligned(tables->arena, PAGE_TABLE_BYTES,
                                          PAGE_BYTES);
    if (!memory) return NULL;
    memset(memory, 0, PAGE_TABLE_BYTES);
    return (uint64_t *)memory;
}

bool paging_create(struct page_tables *tables, struct arena *arena,
                   bool huge_pages) {
    if (!tables || !arena) return false;

    tables->arena = arena;
    tables->huge_pages = huge_pages;

    uint64_t *root = allocate_table(tables);
    if (!root) return false;

    tables->root = (uint64_t)(uintptr_t)root;
    return true;
}

/* The table one level down, made if it is not there. Fails rather than
   descending through an entry that is already a large page: that address is
   mapped, and this would be splitting a mapping nobody asked to split. */
static uint64_t *descend(struct page_tables *tables, uint64_t *table,
                         unsigned level, uint64_t virtual, bool create) {
    uint64_t *entry = &table[index_at(virtual, level)];

    if (*entry & PAGE_PRESENT) {
        if (level > PAGE_LEVEL_TABLE && (*entry & PAGE_SIZE_LARGE)) return NULL;
        return table_of(*entry & PAGE_ADDRESS_MASK);
    }
    if (!create) return NULL;

    uint64_t *next = allocate_table(tables);
    if (!next) return NULL;

    *entry = ((uint64_t)(uintptr_t)next & PAGE_ADDRESS_MASK) |
             PAGE_INTERMEDIATE_FLAGS;
    return next;
}

/* The largest page that fits at this point: aligned on both sides, with enough
   left to fill, and supported by the processor. */
static uint64_t page_size_for(const struct page_tables *tables, uint64_t virtual,
                              uint64_t physical, uint64_t remaining) {
    if (tables->huge_pages && remaining >= PAGE_HUGE_BYTES &&
        (virtual % PAGE_HUGE_BYTES) == 0 && (physical % PAGE_HUGE_BYTES) == 0) {
        return PAGE_HUGE_BYTES;
    }
    if (remaining >= PAGE_LARGE_BYTES && (virtual % PAGE_LARGE_BYTES) == 0 &&
        (physical % PAGE_LARGE_BYTES) == 0) {
        return PAGE_LARGE_BYTES;
    }
    return PAGE_BYTES;
}

static unsigned level_for(uint64_t page_size) {
    if (page_size == PAGE_HUGE_BYTES) return PAGE_LEVEL_POINTER;
    if (page_size == PAGE_LARGE_BYTES) return PAGE_LEVEL_DIRECTORY;
    return PAGE_LEVEL_TABLE;
}

static bool map_one(struct page_tables *tables, uint64_t virtual,
                    uint64_t physical, uint64_t page_size, uint64_t flags) {
    unsigned leaf_level = level_for(page_size);
    uint64_t *table = table_of(tables->root);

    for (unsigned level = PAGE_LEVEL_ROOT; level > leaf_level; level--) {
        table = descend(tables, table, level, virtual, true);
        if (!table) return false;
    }

    uint64_t *entry = &table[index_at(virtual, leaf_level)];
    if (*entry & PAGE_PRESENT) return false;

    uint64_t value = (physical & PAGE_ADDRESS_MASK) | flags | PAGE_PRESENT;
    if (leaf_level != PAGE_LEVEL_TABLE) value |= PAGE_SIZE_LARGE;
    *entry = value;
    return true;
}

bool paging_map(struct page_tables *tables, uint64_t virtual, uint64_t physical,
                uint64_t bytes, uint64_t flags) {
    if (!tables || !tables->root) return false;
    if (bytes == 0) return false;
    if ((virtual % PAGE_BYTES) != 0 || (physical % PAGE_BYTES) != 0) return false;
    if ((bytes % PAGE_BYTES) != 0) return false;

    uint64_t virtual_end, physical_end;
    if (!checked_add_u64(virtual, bytes, &virtual_end)) return false;
    if (!checked_add_u64(physical, bytes, &physical_end)) return false;

    /* The last byte as well as the first: a range that starts canonical and
       runs into the hole is not a range the processor can translate. */
    if (!paging_address_is_canonical(virtual)) return false;
    if (!paging_address_is_canonical(virtual_end - 1U)) return false;

    /* Flags may not carry an address, and PAGE_SIZE_LARGE is this code's to
       set: a caller passing it would make a 4 KiB entry claim to be a page. */
    if (flags & (PAGE_ADDRESS_MASK | PAGE_SIZE_LARGE)) return false;

    uint64_t remaining = bytes;
    while (remaining > 0) {
        uint64_t page_size = page_size_for(tables, virtual, physical, remaining);
        if (!map_one(tables, virtual, physical, page_size, flags)) return false;

        virtual += page_size;
        physical += page_size;
        remaining -= page_size;
    }
    return true;
}

bool paging_translate(const struct page_tables *tables, uint64_t virtual,
                      uint64_t *physical, uint64_t *flags) {
    if (!tables || !tables->root) return false;
    if (!paging_address_is_canonical(virtual)) return false;

    uint64_t *table = table_of(tables->root);
    for (unsigned level = PAGE_LEVEL_ROOT; level >= PAGE_LEVEL_TABLE; level--) {
        uint64_t entry = table[index_at(virtual, level)];
        if (!(entry & PAGE_PRESENT)) return false;

        bool leaf = level == PAGE_LEVEL_TABLE ||
                    (level < PAGE_LEVEL_ROOT && (entry & PAGE_SIZE_LARGE));
        if (leaf) {
            uint64_t span = level == PAGE_LEVEL_TABLE ? PAGE_BYTES
                                                      : level_span(level);
            uint64_t base = entry & PAGE_ADDRESS_MASK;
            if (physical) *physical = base + (virtual % span);
            if (flags) *flags = entry & ~PAGE_ADDRESS_MASK;
            return true;
        }
        table = table_of(entry & PAGE_ADDRESS_MASK);
    }
    return false;
}

void paging_activate(const struct page_tables *tables) {
    control_set_page_table_root(tables->root);
}
