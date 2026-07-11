project(
  'tunix-boot',
  'c',
  version: '0.1.0',
  meson_version: '>=1.1.0',
  default_options: [
    'c_std=c2x',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
  ],
)

python = import('python').find_installation('python3')

freestanding_arguments = [
  '-ffreestanding',
  '-fno-stack-protector',
  '-fno-strict-aliasing',
  '-fno-builtin',
  '-fno-common',
  '-mno-red-zone',
  '-mno-mmx',
  '-mno-sse',
  '-mno-sse2',
  '-Wshadow',
  '-Wconversion',
  '-Wsign-conversion',
  '-Wvla',
  '-Wundef',
  '-Wmissing-prototypes',
  '-Wstrict-prototypes',
]

host_arguments = [
  '-Wshadow',
  '-Wconversion',
  '-Wsign-conversion',
  '-Wvla',
  '-Wundef',
  '-Wmissing-prototypes',
  '-Wstrict-prototypes',
]

building_for_host = meson.is_cross_build() == false
compiler = meson.get_compiler('c')

if building_for_host
  common_arguments = compiler.get_supported_arguments(host_arguments)
else
  common_arguments = compiler.get_supported_arguments(freestanding_arguments)
endif

add_project_arguments(common_arguments, language: 'c')

include_directories_root = include_directories('src', 'include')

nasm = find_program('nasm', required: not building_for_host)

# A kernel for the loader to load. Built by the real toolchain, so the image the
# loader is tested against is one it did not help produce.
if not building_for_host
  subdir('tests/kernel')
endif

subdir('src')

if get_option('tests') and building_for_host
  subdir('tests/unit')
endif

test(
  'layering',
  python,
  args: [meson.project_source_root() / 'tools' / 'check-layering.py'],
  suite: 'static',
)
