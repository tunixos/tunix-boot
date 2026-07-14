stage1 = custom_target(
  'stage1.bin',
  input: 'stage1.asm',
  output: 'stage1.bin',
  command: [nasm, '-f', 'bin', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true,
)

stage2_entry = custom_target(
  'stage2.o',
  input: 'stage2.asm',
  output: 'stage2.o',
  command: [nasm, '-f', 'elf64', '@INPUT@', '-o', '@OUTPUT@'],
)

stage2_sources = files(
  '../../boot/main.c',
  '../../boot/bios_entry.c',
  '../../log/log.c',
  '../../serial/serial.c',
  'fw_bios.c',
  '../../block/ata_port.c',
)

stage2 = executable(
  'stage2.elf',
  stage2_sources,
  stage2_entry,
  dependencies: util_dependency,
  link_args: [
    '-T', meson.current_source_dir() / 'linker.ld',
    '-Wl,-z,max-page-size=0x1000',
    # Code and data share one segment by design here; there is no loader to
    # apply permissions and nothing yet to protect them from.
    '-Wl,--no-warn-rwx-segments',
  ],
  link_depends: files('linker.ld'),
)

# Stage2 is loaded by a boot record that knows nothing about ELF, so it ships as
# a flat image at the address the linker script placed it.
stage2_binary = custom_target(
  'stage2.bin',
  input: stage2,
  output: 'stage2.bin',
  command: [find_program('objcopy'), '-O', 'binary', '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
)

bios_image = custom_target(
  'tunix-boot.img',
  input: [stage1, stage2_binary, test_kernel],
  output: 'tunix-boot.img',
  command: [
    python, meson.project_source_root() / 'tools' / 'make-image.py',
    '@OUTPUT@', '@INPUT0@', '@INPUT1@', '@INPUT2@',
  ],
  # The script decides the partition layout and carries the configuration the
  # loader reads, so editing it changes the image. Without this, it does not.
  depend_files: files(meson.project_source_root() / 'tools' / 'make-image.py'),
  build_by_default: true,
)

qemu = find_program('qemu-system-x86_64', required: false)
if qemu.found()
  test(
    'bios-boot',
    find_program(meson.project_source_root() / 'tests' / 'qemu' / 'boot-test.sh'),
    args: [bios_image],
    suite: 'qemu',
    timeout: 60,
  )
endif
