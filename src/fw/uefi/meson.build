# No stage1 and no stage2: the firmware loads this image directly, so the whole
# loader is one artefact.
uefi_sources = files(
  '../../boot/main.c',
  '../../boot/uefi_entry.c',
  '../../log/log.c',
  '../../serial/serial.c',
  'fw_uefi.c',
  # Port I/O is the same under either firmware, so both backends carry the
  # driver that uses it; it stays out of the host library because it is the
  # one place that really touches hardware.
  '../../block/ata_port.c',
  '../../pci/pci_port.c',
)

uefi_backend = static_library(
  'tunix-boot-uefi',
  uefi_sources,
  dependencies: util_dependency,
)

# Linked by hand rather than with executable(), because meson insists on
# passing GNU ld flags — --as-needed, --no-undefined, --allow-shlib-undefined —
# that lld-link warns about one at a time whichever way its options are set.
# An EFI application wants none of them, and this way nothing arrives unasked.
uefi_application = custom_target(
  'BOOTX64.EFI',
  input: [uefi_backend, core_library],
  output: 'BOOTX64.EFI',
  command: [
    find_program('lld-link'),
    '-machine:x64',
    '-subsystem:efi_application',
    '-entry:efi_main',
    '-nodefaultlib',
    '@INPUT@',
    '-out:@OUTPUT@',
  ],
  build_by_default: true,
)

# The partition the loader reads. Under UEFI the firmware loads the
# application off its own ESP, so this image carries no boot code at all.
data_image = custom_target(
  'tunix-data.img',
  input: test_kernel,
  output: 'tunix-data.img',
  command: [
    python, meson.project_source_root() / 'tools' / 'make-image.py',
    '@OUTPUT@', '@INPUT@',
  ],
  depend_files: files(meson.project_source_root() / 'tools' / 'make-image.py'),
  build_by_default: true,
)

qemu = find_program('qemu-system-x86_64', required: false)
ovmf_code = '/usr/share/edk2/x64/OVMF_CODE.4m.fd'
ovmf_vars = '/usr/share/edk2/x64/OVMF_VARS.4m.fd'

if qemu.found() and import('fs').exists(ovmf_code)
  test(
    'uefi-boot',
    find_program(meson.project_source_root() / 'tests' / 'qemu' / 'uefi-test.sh'),
    args: [uefi_application, data_image, ovmf_code, ovmf_vars],
    suite: 'qemu',
    timeout: 90,
  )
endif
