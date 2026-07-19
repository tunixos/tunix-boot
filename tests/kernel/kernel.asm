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
    jmp .protocol

.dirty:
    mov rsi, dirty_message
    call write

    ; Everything below came from the loader through the boot protocol. A null
    ; response means the loader never answered, which is worth saying out loud.
.protocol:
    mov rax, [loader_info_request + REQUEST_RESPONSE]
    test rax, rax
    jz .missing

    mov rsi, loaded_by
    call write
    mov rsi, [rax + RESPONSE_LOADER_NAME]
    call write
    mov rsi, space
    call write
    mov rsi, [rax + RESPONSE_LOADER_VERSION]
    call write
    mov rsi, newline
    call write

    mov rax, [command_line_request + REQUEST_RESPONSE]
    test rax, rax
    jz .missing

    mov rsi, cmdline_label
    call write
    mov rsi, [rax + RESPONSE_COMMAND_LINE]
    call write
    mov rsi, newline
    call write

    mov rax, [memory_map_request + REQUEST_RESPONSE]
    test rax, rax
    jz .missing
    ; A map with no entries describes no memory, which is not a map.
    cmp qword [rax + RESPONSE_MEMORY_COUNT], 0
    je .missing

    mov rsi, memory_message
    call write

    ; A screen is optional, so a null response here is an answer and not a fault.
    mov rax, [framebuffer_request + REQUEST_RESPONSE]
    test rax, rax
    jz .no_screen
    cmp qword [rax + RESPONSE_SCREEN_BASE], 0
    je .no_screen
    cmp dword [rax + RESPONSE_SCREEN_WIDTH], 0
    je .no_screen

    mov rsi, screen_message
    call write
    jmp .halt

.no_screen:
    mov rsi, no_screen_message
    call write
    jmp .halt

.missing:
    mov rsi, unanswered
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

; What this kernel asks the loader for. The loader finds these by scanning the
; image for the magic pair, so they need no fixed address and no header — a
; kernel asks for what it understands and nothing else.
SECTION .data
ALIGN 8

REQUEST_MAGIC_LOW    equ 0x5449554e49582d31
REQUEST_MAGIC_HIGH   equ 0x424f4f54503a3031
REQUEST_MEMORY_MAP   equ 1
REQUEST_COMMAND_LINE equ 3
REQUEST_LOADER_INFO  equ 4
REQUEST_FRAMEBUFFER  equ 5

; Offsets into the responses, which are what the ABI actually promises.
RESPONSE_MEMORY_COUNT   equ 8
RESPONSE_COMMAND_LINE   equ 8
RESPONSE_LOADER_NAME    equ 8
RESPONSE_LOADER_VERSION equ 16
RESPONSE_SCREEN_BASE    equ 8
RESPONSE_SCREEN_WIDTH   equ 16
REQUEST_RESPONSE        equ 32

memory_map_request:
    dq REQUEST_MAGIC_LOW, REQUEST_MAGIC_HIGH
    dq REQUEST_MEMORY_MAP, 0, 0

command_line_request:
    dq REQUEST_MAGIC_LOW, REQUEST_MAGIC_HIGH
    dq REQUEST_COMMAND_LINE, 0, 0

loader_info_request:
    dq REQUEST_MAGIC_LOW, REQUEST_MAGIC_HIGH
    dq REQUEST_LOADER_INFO, 0, 0

framebuffer_request:
    dq REQUEST_MAGIC_LOW, REQUEST_MAGIC_HIGH
    dq REQUEST_FRAMEBUFFER, 0, 0

SECTION .rodata
message:        db "kernel running", 10, 0
clean_message:  db "kernel bss was zeroed", 10, 0
dirty_message:  db "kernel bss was NOT zeroed", 10, 0
loaded_by:      db "loaded by ", 0
space:          db " ", 0
cmdline_label:  db "cmdline: ", 0
memory_message: db "memory map received", 10, 0
screen_message: db "framebuffer received", 10, 0
no_screen_message: db "no framebuffer", 10, 0
newline:        db 10, 0
unanswered:     db "a request went unanswered", 10, 0

SECTION .bss
zero_check: resq 1
