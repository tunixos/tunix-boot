# Architecture

## Why a firmware abstraction, and where it stops

BIOS and UEFI are not two flavours of the same interface. BIOS hands control to
512 bytes in 16-bit real mode and offers services through software interrupts.
UEFI loads a PE32+ application that is already in long mode with paging enabled,
gives it a table of protocols, and requires `ExitBootServices` before the loaded
kernel may touch the machine.

The core therefore never calls firmware directly. It talks to `struct fw_ops`:

    mem_snapshot          the memory map as it stands, for planning
    block_read            sectors from a firmware-known disk
    console_write         early output before a framebuffer exists
    framebuffer_acquire   a linear framebuffer in a requested mode
    rsdp_locate           the ACPI root pointer
    exit_firmware         leave firmware control, returning the final map

The alternative is GRUB2's approach: one binary that dispatches at run time.
That was rejected. The BIOS path needs real-mode thunks and the UEFI path needs
the Microsoft calling convention; a single binary carries the dead weight of
both. Two artefacts from one core costs a second build target and buys a core
that is smaller, and testable on the host against a mock `fw_ops`.

Two details are deliberately visible in the interface rather than hidden:

*Getting the memory map is not idempotent under UEFI.* The map must be fetched
immediately before `ExitBootServices`, and the key it returns must match. So the
interface has no "fetch the map" call that pretends otherwise: `mem_snapshot` is
for planning and `exit_firmware` returns the map that is actually handed on.

*The framebuffer must be acquired before leaving firmware.* GOP is a boot
service. After `ExitBootServices` there is no way to ask for one.

## Layering

    util   <- everyone
    cpu    <- memory, smp, protocol
    memory <- fw, block, fs, elf, protocol
    fw     <- block, video, acpi, protocol
    block  <- fs
    fs     <- config, elf
    elf    <- protocol
    video  <- terminal
    acpi   <- smp
    config <- boot
    protocol <- boot
    boot   <- nothing depends on it

`log` and `serial` are leaves: they depend on `util` and nothing else, so any
layer may use them without creating a cycle.

A lower layer may not include a higher layer's header. `tools/check-layering.py`
reads these rules from `docs/layers.txt` and fails the build if an include
crosses them.

## Memory

Two tiers, because a bootloader that uses a general purpose heap turns
fragmentation and leaks into problems that cannot be debugged from a machine
with no debugger.

*Arena.* A bump allocator for everything that lives for the duration of the
boot. Allocation is a pointer increment; there is no free. What it costs in
flexibility it returns in auditability.

*Page allocator.* Underneath the arena. Under UEFI it goes through
`AllocatePages`, because taking memory the firmware does not know about is
undefined before `ExitBootServices`. Under BIOS the loader owns the E820 map.

A separate scratch arena serves parsing, and is reset wholesale when the parse
is finished.

## Untrusted input

Everything on disk is hostile until proven otherwise: partition tables,
filesystem metadata, ELF headers, ACPI tables, the configuration file. The rule
is that raw pointer arithmetic over such data is not allowed. Reads go through
`struct reader`, which carries a length and refuses to run past it, and
arithmetic on sizes derived from disk goes through the checked helpers.

Stack canaries are honest about what they are: before ACPI there is no good
entropy source, so the canary uses RDRAND where available and the timestamp
counter otherwise. It raises the cost of an exploit; the bounds checks are what
actually prevent one.

## Initialisation order

    firmware entry
    cpu_early              cpuid, feature detection
    serial, log
    firmware backend init
    memory map, arena
    block devices
    filesystems
    configuration
    menu or timeout
    kernel image load
    paging: identity and higher half
    protocol structures
    acpi
    smp trampoline
    framebuffer                (before leaving firmware)
    exit_firmware
    handoff
