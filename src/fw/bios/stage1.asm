; The master boot record: 512 bytes that must find the rest of the loader.
;
; It does one job, and the constraint is brutal enough that the job has to stay
; small: read stage2 off the same disk the BIOS booted from, and jump to it.
; Anything that can be done later is done later.

BITS 16
ORG 0x7C00

STACK_TOP             equ 0x7C00      ; grows down, below where we are loaded
STAGE2_LOAD_SEGMENT   equ 0x0000
STAGE2_LOAD_OFFSET    equ 0x7E00      ; directly after this sector
STAGE2_START_LBA      equ 1
BOOT_SIGNATURE        equ 0xAA55
SECTOR_BYTES          equ 512

INT_DISK              equ 0x13
DISK_CHECK_EXTENSIONS equ 0x41
DISK_EXTENDED_READ    equ 0x42
EXTENSIONS_MAGIC      equ 0x55AA
EXTENSIONS_REPLY      equ 0xAA55

INT_VIDEO             equ 0x10
VIDEO_TELETYPE        equ 0x0E

; Read in chunks, so this is only bounded by where stage2 is allowed to land.
SECTORS_PER_READ      equ 64
PARAGRAPHS_PER_SECTOR equ 32          ; 512 bytes / 16

; Offsets into the disk address packet, which the read takes in memory.
DAP_SECTORS           equ 2
DAP_OFFSET            equ 4
DAP_SEGMENT           equ 6
DAP_LBA               equ 8

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 256
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    cld
    sti

    ; The BIOS hands us the drive we came from in DL. Nothing else knows it.
    mov [boot_drive], dl

    mov ah, DISK_CHECK_EXTENSIONS
    mov bx, EXTENSIONS_MAGIC
    mov dl, [boot_drive]
    int INT_DISK
    jc .no_extensions
    cmp bx, EXTENSIONS_REPLY
    jne .no_extensions

    ; A chunk at a time. Many BIOSes refuse more than 127 sectors in one
    ; extended read, and stage2 passed that some time ago.
    mov cx, STAGE2_SECTORS

.read_chunk:
    mov ax, SECTORS_PER_READ
    cmp cx, ax
    jae .full_chunk
    mov ax, cx
.full_chunk:
    mov [disk_address_packet + DAP_SECTORS], ax

    push ax
    push cx
    mov si, disk_address_packet
    mov ah, DISK_EXTENDED_READ
    mov dl, [boot_drive]
    int INT_DISK
    pop cx
    pop ax
    jc .read_failed

    ; Onwards by what was just read: the LBA in sectors, and the destination in
    ; paragraphs, which is what lets this reach past one segment.
    add word [disk_address_packet + DAP_LBA], ax
    adc word [disk_address_packet + DAP_LBA + 2], 0

    push ax
    mov bx, PARAGRAPHS_PER_SECTOR
    mul bx
    add [disk_address_packet + DAP_SEGMENT], ax
    pop ax

    sub cx, ax
    jnz .read_chunk

    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEGMENT:STAGE2_LOAD_OFFSET

.no_extensions:
    mov si, message_no_extensions
    jmp fail

.read_failed:
    mov si, message_read_failed
    jmp fail

; Print and stop. There is nowhere to return to.
fail:
    call print
    cli
.halt:
    hlt
    jmp .halt

print:
    mov ah, VIDEO_TELETYPE
    xor bx, bx
.next:
    lodsb
    test al, al
    jz .done
    int INT_VIDEO
    jmp .next
.done:
    ret

align 4
; The extended read takes its arguments in memory rather than registers.
disk_address_packet:
    db 0x10                     ; packet size
    db 0
    dw STAGE2_SECTORS
    dw STAGE2_LOAD_OFFSET
    dw STAGE2_LOAD_SEGMENT
    dq STAGE2_START_LBA

boot_drive:            db 0
message_no_extensions: db "no int13h extensions", 0
message_read_failed:   db "stage2 read failed", 0

times SECTOR_BYTES - 2 - ($ - $$) db 0
dw BOOT_SIGNATURE
