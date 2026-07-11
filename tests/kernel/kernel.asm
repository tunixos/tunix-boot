; The smallest thing that proves a kernel was loaded and entered.
;
; Built by the real toolchain rather than assembled byte by byte in a test, so
; it exercises what a linker actually emits — a program header table the loader
; has to walk, a .bss the loader has to zero, and an entry point that is not the
; start of the image.

BITS 64
DEFAULT ABS

SERIAL_PORT          equ 0x3F8
SERIAL_LINE_STATUS   equ SERIAL_PORT + 5
SERIAL_TRANSMIT_IDLE equ 1 << 5

SECTION .text
GLOBAL _start

; Placed after the string so the entry point is not the first byte of the
; segment; a loader that jumps to the load address instead of e_entry lands here
; and runs the message as code.
_start:
    mov rsi, message
    call write

    ; Proof the loader zeroed what the file did not cover: this lives in .bss,
    ; and anything but zero means the memory was handed over as it was found.
    mov rax, [zero_check]
    test rax, rax
    jnz .dirty

    mov rsi, clean_message
    call write
    jmp .halt

.dirty:
    mov rsi, dirty_message
    call write

.halt:
    cli
    hlt
    jmp .halt

write:
    push rax
    push rdx
.next:
    lodsb
    test al, al
    jz .done
.wait:
    mov dx, SERIAL_LINE_STATUS
    in al, dx
    test al, SERIAL_TRANSMIT_IDLE
    jz .wait
    mov al, [rsi - 1]
    mov dx, SERIAL_PORT
    out dx, al
    jmp .next
.done:
    pop rdx
    pop rax
    ret

SECTION .rodata
message:       db "kernel running", 10, 0
clean_message: db "kernel bss was zeroed", 10, 0
dirty_message: db "kernel bss was NOT zeroed", 10, 0

SECTION .bss
zero_check: resq 1
