/* Stage2 is loaded by the master boot record at a fixed address and has no
   loader of its own, so the layout is spelled out here rather than negotiated. */
ENTRY(stage2_start)

STAGE2_BASE = 0x7E00;

SECTIONS
{
    . = STAGE2_BASE;

    .stage2entry : { *(.stage2entry) }
    .text        : { *(.text .text.*) }
    .rodata      : { *(.rodata .rodata.*) }
    .data        : { *(.data .data.*) }
    .bss         : { *(.bss .bss.*) *(COMMON) }

    stage2_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
