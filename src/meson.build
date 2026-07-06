util_sources = files(
  'util/mem.c',
  'util/reader.c',
  'util/str.c',
)

# cpuid.c and gdt.c reach hardware only through function pointers the tests
# replace, so they build for the host as well as the target.
memory_sources = files(
  'memory/arena.c',
  'memory/map.c',
)

cpu_sources = files(
  'cpu/cpuid.c',
  'cpu/gdt.c',
)

core_library = static_library(
  'tunix-boot-core',
  util_sources + cpu_sources + memory_sources,
  include_directories: include_directories_root,
)

util_dependency = declare_dependency(
  link_with: core_library,
  include_directories: include_directories_root,
)

if not building_for_host
  subdir('fw/bios')
endif
