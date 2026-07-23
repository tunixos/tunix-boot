/* A kernel where kernels actually live: the top two gigabytes of the address
   space, loaded into low physical memory. AT() is what splits the two, giving
   each program header a virtual address in the higher half and a physical one
   the loader can write to before paging is switched.
 *
 * The sections are put in separate segments with separate permissions, which is
 * the point: it gives the loader a read-only segment and a writable one to map
 * differently, so W^X and the no-execute bit are actually exercised rather than
 * being code nothing reaches.
 */
ENTRY(_start)

KERNEL_VIRTUAL  = 0xFFFFFFFF80000000;
KERNEL_PHYSICAL = 0x400000;

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* read + execute */
    rodata PT_LOAD FLAGS(4);   /* read only */
    data   PT_LOAD FLAGS(6);   /* read + write */
}

SECTIONS
{
    . = KERNEL_VIRTUAL;

    .text : AT(ADDR(.text) - KERNEL_VIRTUAL + KERNEL_PHYSICAL) {
        *(.text)
    } :text

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRTUAL + KERNEL_PHYSICAL) {
        *(.rodata)
    } :rodata

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VIRTUAL + KERNEL_PHYSICAL) {
        *(.data)
    } :data
    .bss : AT(ADDR(.bss) - KERNEL_VIRTUAL + KERNEL_PHYSICAL) {
        *(.bss) *(COMMON)
    } :data

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
