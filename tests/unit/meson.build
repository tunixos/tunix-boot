unit_tests = [
  ['reader', 'test_reader.c'],
  ['str', 'test_str.c'],
  ['checked', 'test_checked.c'],
  ['cpuid', 'test_cpuid.c'],
  ['gdt', 'test_gdt.c'],
  ['arena', 'test_arena.c'],
  ['map', 'test_map.c'],
  ['e820', 'test_e820.c'],
  ['fw', 'test_fw.c'],
  ['block', 'test_block.c'],
  ['ata', 'test_ata.c'],
  ['partition', 'test_partition.c'],
  ['fat', 'test_fat.c'],
  ['ext2', 'test_ext2.c'],
  ['elf', 'test_elf.c'],
  ['paging', 'test_paging.c'],
  ['config', 'test_config.c'],
]

foreach entry : unit_tests
  name = entry[0]
  executable_target = executable(
    'test-' + name,
    entry[1],
    dependencies: util_dependency,
    include_directories: include_directories('.'),
  )
  test(name, executable_target, suite: 'unit')
endforeach
