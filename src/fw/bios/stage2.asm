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

; Above anything stage2 itself can occupy. stage2 loads at 0x7E00 and its .bss
; grows with the core, so a base just past the loaded image is a base that one
; day gets zeroed by the .bss clear below. linker.ld asserts this stays true.
PAGE_TABLE_BASE      equ 0x40000
PAGE_TABLE_SEGMENT   equ PAGE_TABLE_BASE >> 4
PML4_OFFSET          equ 0
PDPT_OFFSET          equ 0x1000
PAGE_DIRECTORY_OFFSET equ 0x2000
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

; INT 15h AX=E820h is a real-mode service, so the map is collected here and left
; where the core can parse it. Kept clear of the page tables at 0x10000.
E820_BUFFER_ADDRESS  equ 0x30000
E820_BUFFER_SEGMENT  equ E820_BUFFER_ADDRESS >> 4
E820_FUNCTION        equ 0xE820
E820_SIGNATURE       equ 0x534D4150     ; 'SMAP'
E820_ENTRY_BYTES     equ 24
E820_MAX_ENTRIES     equ 128
E820_ATTRIBUTE_OFFSET equ 20
E820_ATTRIBUTE_VALID equ 1

; INT 10h is real-mode too, so the mode is chosen and set here and the block the
; card gave us is left where the core can read it. Clear of the E820 buffer,
; which ends at 0x30C00.
VBE_CONTROLLER_ADDRESS equ 0x31000
VBE_CONTROLLER_SEGMENT equ VBE_CONTROLLER_ADDRESS >> 4
VBE_MODE_INFO_ADDRESS  equ 0x31400
VBE_MODE_INFO_SEGMENT  equ VBE_MODE_INFO_ADDRESS >> 4

VBE_GET_CONTROLLER   equ 0x4F00
VBE_GET_MODE_INFO    equ 0x4F01
VBE_SET_MODE         equ 0x4F02
VBE_SUPPORTED        equ 0x004F           ; what AX reads back on success
VBE_SIGNATURE        equ 0x32454256       ; 'VBE2', asking for the newer block
VBE_MODE_LIST_OFFSET equ 14
VBE_MODE_END         equ 0xFFFF
VBE_USE_LINEAR       equ 1 << 14

; Offsets into the mode information block. Kept in step with fw/bios/vbe.h,
; which is what actually interprets them.
VBE_INFO_ATTRIBUTES  equ 0x00
VBE_INFO_WIDTH       equ 0x12
VBE_INFO_HEIGHT      equ 0x14
VBE_INFO_DEPTH       equ 0x19
VBE_INFO_MODEL       equ 0x1B

VBE_ATTR_SUPPORTED   equ 1 << 0
VBE_ATTR_GRAPHICS    equ 1 << 4
VBE_ATTR_LINEAR      equ 1 << 7
VBE_ATTR_WANTED      equ VBE_ATTR_SUPPORTED | VBE_ATTR_GRAPHICS | VBE_ATTR_LINEAR
VBE_MODEL_DIRECT     equ 6
VBE_WANTED_DEPTH     equ 32

; What to ask for. Anything else acceptable is kept as a fallback, so a card
; without this exact mode still gets a screen.
VBE_PREFERRED_WIDTH  equ 1280
VBE_PREFERRED_HEIGHT equ 1024

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
    call collect_memory_map
    call set_video_mode
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

; Collect raw E820 entries and count them. Nothing here judges what the firmware
; said: validation is e820_parse's job, where it can be tested.
collect_memory_map:
    pushad
    push es

    mov ax, E820_BUFFER_SEGMENT
    mov es, ax
    xor di, di
    xor ebx, ebx
    xor bp, bp

.next_entry:
    ; Seeded set, so firmware that answers with 20 bytes and never writes the
    ; attributes word does not have its entries read as "ignore me".
    mov dword [es:di + E820_ATTRIBUTE_OFFSET], E820_ATTRIBUTE_VALID

    mov eax, E820_FUNCTION
    mov edx, E820_SIGNATURE
    mov ecx, E820_ENTRY_BYTES
    int 0x15

    ; Carry on the first call means the service does not exist; on a later one
    ; it means the list ended. Both leave bp holding what we did get.
    jc .done
    cmp eax, E820_SIGNATURE
    jne .done
    test ecx, ecx
    jz .done

    inc bp
    add di, E820_ENTRY_BYTES
    cmp bp, E820_MAX_ENTRIES
    jae .done

    ; A zero continuation is the firmware saying that entry was the last.
    test ebx, ebx
    jnz .next_entry

.done:
    mov [memory_map_count], bp
    pop es
    popad
    ret

; Finds a linear-framebuffer mode, sets it, and leaves its information block at
; VBE_MODE_INFO_ADDRESS. Judges only what it must to choose between modes; the
; block is validated in C, where that can be tested.
set_video_mode:
    pushad
    push es

    mov ax, VBE_CONTROLLER_SEGMENT
    mov es, ax
    xor di, di
    ; Asking for the VBE 2 block, which is what carries the linear address.
    mov dword [es:di], VBE_SIGNATURE
    mov ax, VBE_GET_CONTROLLER
    int 0x10
    cmp ax, VBE_SUPPORTED
    jne .done

    ; The mode list is a far pointer, and the segment it points into is not
    ; necessarily the one the block is in.
    mov si, [es:VBE_MODE_LIST_OFFSET]
    mov ax, [es:VBE_MODE_LIST_OFFSET + 2]
    mov fs, ax

    xor bp, bp                      ; the fallback mode, 0 for none

.next_mode:
    mov cx, [fs:si]
    add si, 2
    cmp cx, VBE_MODE_END
    je .choose

    push cx
    push si
    mov ax, VBE_MODE_INFO_SEGMENT
    mov es, ax
    xor di, di
    mov ax, VBE_GET_MODE_INFO
    int 0x10
    pop si
    pop cx
    cmp ax, VBE_SUPPORTED
    jne .next_mode

    mov ax, [es:VBE_INFO_ATTRIBUTES]
    and ax, VBE_ATTR_WANTED
    cmp ax, VBE_ATTR_WANTED
    jne .next_mode
    cmp byte [es:VBE_INFO_DEPTH], VBE_WANTED_DEPTH
    jne .next_mode
    cmp byte [es:VBE_INFO_MODEL], VBE_MODEL_DIRECT
    jne .next_mode

    ; Acceptable. Take it outright if it is the size we asked for, otherwise
    ; keep it and carry on looking.
    mov bp, cx
    cmp word [es:VBE_INFO_WIDTH], VBE_PREFERRED_WIDTH
    jne .next_mode
    cmp word [es:VBE_INFO_HEIGHT], VBE_PREFERRED_HEIGHT
    jne .next_mode
    jmp .set

.choose:
    test bp, bp
    jz .done

.set:
    ; The information block in memory is whichever mode was looked at last, so
    ; the chosen one is fetched again before it is set.
    mov cx, bp
    push cx
    mov ax, VBE_MODE_INFO_SEGMENT
    mov es, ax
    xor di, di
    mov ax, VBE_GET_MODE_INFO
    int 0x10
    pop cx
    cmp ax, VBE_SUPPORTED
    jne .failed

    mov bx, cx
    or bx, VBE_USE_LINEAR
    mov ax, VBE_SET_MODE
    int 0x10
    cmp ax, VBE_SUPPORTED
    je .done

.failed:
    ; A block of zeroes is a mode the core will refuse, which is what should
    ; happen when the mode could not be set.
    mov ax, VBE_MODE_INFO_SEGMENT
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 128
    rep stosw

.done:
    pop es
    popad
    ret

; Every access here is ES-relative with a 16-bit offset, which is all a
; real-mode segment allows. A flat 32-bit offset reaches the same bytes under
; emulation and general-protection faults on a processor, which is a difference
; that does not show up until the day something runs this for real.
;
; The addresses written *into* the entries are still full physical addresses;
; only the addressing used to place them is segmented.
build_page_tables:
    pushad
    push es

    mov ax, PAGE_TABLE_SEGMENT
    mov es, ax

    xor di, di
    mov cx, (PAGE_DIRECTORY_COUNT + 2) * PAGE_TABLE_BYTES / 4
    xor eax, eax
    rep stosd

    mov di, PML4_OFFSET
    mov dword [es:di], PDPT_ADDRESS | PAGE_PRESENT | PAGE_WRITABLE

    mov cx, PAGE_DIRECTORY_COUNT
    mov di, PDPT_OFFSET
    mov eax, PAGE_DIRECTORY_BASE | PAGE_PRESENT | PAGE_WRITABLE
.pdpt_entry:
    mov [es:di], eax
    add eax, PAGE_TABLE_BYTES
    add di, 8
    loop .pdpt_entry

    ; Every directory entry is a 2 MiB page mapped to itself.
    mov di, PAGE_DIRECTORY_OFFSET
    mov cx, PAGE_DIRECTORY_COUNT * ENTRIES_PER_TABLE
    xor eax, eax
.directory_entry:
    mov ebx, eax
    or ebx, PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE
    mov [es:di], ebx
    add eax, IDENTITY_PAGE_BYTES
    add di, 8
    loop .directory_entry

    pop es
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
    xor rsi, rsi
    mov si, [memory_map_count]
    mov edx, E820_BUFFER_ADDRESS
    mov ecx, VBE_MODE_INFO_ADDRESS
    call boot_main

.halt:
    cli
    hlt
    jmp .halt

; Kept in .stage2entry rather than .data. Real-mode code reaches these with a
; 16-bit displacement, and as the core grew .data moved past 64 KiB, where that
; displacement cannot reach — which the linker reports as a truncated
; relocation rather than anything about addressing modes.
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
memory_map_count: dw 0
