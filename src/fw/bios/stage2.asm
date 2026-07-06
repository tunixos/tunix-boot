; Getting from real mode to the C core.
;
; The BIOS leaves us in 16-bit real mode with a megabyte of address space. Long
; mode needs paging enabled before it will switch, and paging needs page tables,
; so the order is forced: A20, page tables, protected mode, PAE, EFER.LME, CR0.PG,
; then a far jump that finally makes the processor 64-bit.
;
; The identity map covers the first four gigabytes with 2 MiB pages. Not 1 GiB
; pages: those need a CPUID feature this code runs too early to have checked,
; and four gigabytes is more than enough to reach anything the firmware left us.

BITS 16
DEFAULT ABS

STACK_TOP            equ 0x7C00

PAGE_TABLE_BASE      equ 0x10000
PML4_ADDRESS         equ PAGE_TABLE_BASE
PDPT_ADDRESS         equ PAGE_TABLE_BASE + 0x1000
PAGE_DIRECTORY_BASE  equ PAGE_TABLE_BASE + 0x2000
PAGE_DIRECTORY_COUNT equ 4
PAGE_TABLE_BYTES     equ 0x1000
ENTRIES_PER_TABLE    equ 512
IDENTITY_PAGE_BYTES  equ 0x200000

PAGE_PRESENT         equ 1 << 0
PAGE_WRITABLE        equ 1 << 1
PAGE_LARGE           equ 1 << 7

CR0_PROTECTED        equ 1 << 0
CR0_PAGING           equ 1 << 31
CR4_PAE              equ 1 << 5
EFER_MSR             equ 0xC0000080
EFER_LONG_MODE       equ 1 << 8

A20_PORT             equ 0x92
A20_ENABLE_BIT       equ 1 << 1

SELECTOR_CODE64      equ 0x08
SELECTOR_DATA64      equ 0x10
SELECTOR_CODE32      equ 0x18
SELECTOR_DATA32      equ 0x20

section .stage2entry
global stage2_start
extern boot_main
extern bss_start
extern bss_end

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    mov [boot_drive], dl

    call enable_a20
    call build_page_tables

    lgdt [gdt_pointer]

    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    mov eax, PML4_ADDRESS
    mov cr3, eax

    mov ecx, EFER_MSR
    rdmsr
    or eax, EFER_LONG_MODE
    wrmsr

    ; Protection and paging together: with EFER.LME already set, this is the
    ; step that puts the processor into long mode's compatibility submode.
    mov eax, cr0
    or eax, CR0_PROTECTED | CR0_PAGING
    mov cr0, eax

    ; dword: the far jump into 64-bit code needs a 32-bit offset, which a
    ; 16-bit encoding cannot hold.
    jmp dword SELECTOR_CODE64:long_mode_entry

; The fast gate first, then the keyboard controller for the machines that do not
; have one. Reading the port back is what tells us which happened.
enable_a20:
    in al, A20_PORT
    test al, A20_ENABLE_BIT
    jnz .done
    or al, A20_ENABLE_BIT
    and al, 0xFE                  ; bit 0 would reset the machine
    out A20_PORT, al
.done:
    ret

build_page_tables:
    pushad

    mov edi, PAGE_TABLE_BASE
    mov ecx, (PAGE_DIRECTORY_COUNT + 2) * PAGE_TABLE_BYTES / 4
    xor eax, eax
    rep stosd

    ; Through a register: a 16-bit displacement cannot hold an address this far
    ; up, and truncating it would put the entry at zero.
    mov edi, PML4_ADDRESS
    mov dword [edi], PDPT_ADDRESS | PAGE_PRESENT | PAGE_WRITABLE

    mov ecx, PAGE_DIRECTORY_COUNT
    mov edi, PDPT_ADDRESS
    mov eax, PAGE_DIRECTORY_BASE | PAGE_PRESENT | PAGE_WRITABLE
.pdpt_entry:
    mov [edi], eax
    add eax, PAGE_TABLE_BYTES
    add edi, 8
    loop .pdpt_entry

    ; Every directory entry is a 2 MiB page mapped to itself.
    mov edi, PAGE_DIRECTORY_BASE
    mov ecx, PAGE_DIRECTORY_COUNT * ENTRIES_PER_TABLE
    xor eax, eax
.directory_entry:
    mov ebx, eax
    or ebx, PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE
    mov [edi], ebx
    add eax, IDENTITY_PAGE_BYTES
    add edi, 8
    loop .directory_entry

    popad
    ret

BITS 64
long_mode_entry:
    mov ax, SELECTOR_DATA64
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, STACK_TOP
    xor rbp, rbp

    ; Nothing has cleared .bss. QEMU hands over zeroed memory and real firmware
    ; does not, so every static in the core would be whatever was there before.
    mov rdi, bss_start
    mov rcx, bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    xor rdi, rdi
    mov dil, [boot_drive]
    call boot_main

.halt:
    cli
    hlt
    jmp .halt

section .data
align 16
; Built here rather than by cpu/gdt.c because the table has to exist before any
; C runs; gdt_build() produces the same descriptors for everything afterwards.
gdt_table:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF          ; 64-bit code
    dq 0x00CF92000000FFFF          ; 64-bit data
    dq 0x00CF9A000000FFFF          ; 32-bit code
    dq 0x00CF92000000FFFF          ; 32-bit data
gdt_end:

gdt_pointer:
    dw gdt_end - gdt_table - 1
    dq gdt_table

boot_drive: db 0
