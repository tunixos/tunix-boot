util_sources = files(
  'util/mem.c',
  'util/reader.c',
  'util/str.c',
)

memory_sources = files(
  'memory/arena.c',
  'memory/map.c',
  'memory/paging.c',
)

block_sources = files(
  'block/block.c',
  'block/ata.c',
  'block/partition.c',
)

protocol_sources = files(
  'protocol/protocol.c',
)

config_sources = files(
  'config/config.c',
)

elf_sources = files(
  'elf/elf.c',
)

fs_sources = files(
  'fs/fat.c',
  'fs/ext2.c',
)

# cpuid.c and gdt.c reach hardware only through function pointers the tests
# replace, so they build for the host as well as the target.
cpu_sources = files(
  'cpu/cpuid.c',
  'cpu/gdt.c',
)

# e820.c parses a buffer somebody else filled in and touches no hardware, so it
# builds for the host and the tests replay real firmware maps through it.
fw_sources = files(
  'fw/fw.c',
  'fw/bios/e820.c',
)

core_library = static_library(
  'tunix-boot-core',
  util_sources + cpu_sources + memory_sources + fw_sources + block_sources + fs_sources + elf_sources + config_sources + protocol_sources,
  include_directories: include_directories_root,
)

util_dependency = declare_dependency(
  link_with: core_library,
  include_directories: include_directories_root,
)

if not building_for_host
  subdir('fw/bios')
endif
