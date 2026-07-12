/* A kernel where kernels actually live: the top two gigabytes of the address
   space, loaded into low physical memory. AT() is what splits the two, giving
   each program header a virtual address in the higher half and a physical one
   the loader can write to before paging is switched. */
ENTRY(_start)

KERNEL_VIRTUAL  = 0xFFFFFFFF80000000;
KERNEL_PHYSICAL = 0x400000;

SECTIONS
{
    . = KERNEL_VIRTUAL;

    .text   : AT(ADDR(.text)   - KERNEL_VIRTUAL + KERNEL_PHYSICAL) { *(.text) }
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRTUAL + KERNEL_PHYSICAL) { *(.rodata) }
    .data   : AT(ADDR(.data)   - KERNEL_VIRTUAL + KERNEL_PHYSICAL) { *(.data) }
    .bss    : AT(ADDR(.bss)    - KERNEL_VIRTUAL + KERNEL_PHYSICAL) { *(.bss) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
