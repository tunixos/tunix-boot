util_sources = files(
  'util/mem.c',
  'util/reader.c',
  'util/str.c',
)

# cpuid.c and gdt.c hold no instruction the host cannot run except through the
# function pointers the tests replace, so they build for both targets.
cpu_sources = files(
  'cpu/cpuid.c',
  'cpu/gdt.c',
)

core_library = static_library(
  'tunix-boot-core',
  util_sources + cpu_sources,
  include_directories: include_directories_root,
)

util_dependency = declare_dependency(
  link_with: core_library,
  include_directories: include_directories_root,
)
