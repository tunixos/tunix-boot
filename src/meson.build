util_sources = files(
  'util/mem.c',
  'util/reader.c',
  'util/str.c',
)

util_library = static_library(
  'tunix-boot-util',
  util_sources,
  include_directories: include_directories_root,
)

util_dependency = declare_dependency(
  link_with: util_library,
  include_directories: include_directories_root,
)
