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
    /* Bracketed so stage2 can zero it: nothing loads this from disk, and the
       BIOS does not promise memory arrives as anything in particular. */
    .bss         : {
        bss_start = .;
        *(.bss .bss.*) *(COMMON)
        bss_end = .;
    }

    stage2_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}

/* Where stage2.asm puts the E820 buffer and the page tables. Growing past it
   means the .bss clear zeroes the page tables after they were built, which
   triple faults with nothing on the serial line to say why. Keep in step with
   E820_BUFFER_ADDRESS in stage2.asm. */
LOW_MEMORY_SCRATCH_BASE = 0x30000;

ASSERT(bss_end <= LOW_MEMORY_SCRATCH_BASE,
       "stage2 has grown into the memory it uses for page tables")
