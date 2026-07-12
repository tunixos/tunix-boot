kernel_object = custom_target(
  'kernel.o',
  input: 'kernel.asm',
  output: 'kernel.o',
  command: [nasm, '-f', 'elf64', '@INPUT@', '-o', '@OUTPUT@'],
)

# Linked where a kernel would ask to be, and by the real linker, so the image
# the loader is tested against is one it did not help produce.
test_kernel = custom_target(
  'kernel.elf',
  input: kernel_object,
  output: 'kernel.elf',
  command: [
    find_program('ld'), '-nostdlib', '-static', '-no-pie',
    '-T', meson.current_source_dir() / 'kernel.ld',
    '--build-id=none', '-z', 'noexecstack', '--no-warn-rwx-segments',
    '@INPUT@', '-o', '@OUTPUT@',
  ],
  depend_files: files('kernel.ld'),
)
