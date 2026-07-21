; test_elf.asm — ELF user program with direct I/O debug

[bits 32]
[global _start]

_start:
    ; Direct serial output — now allowed by TSS I/O bitmap
    mov  dx, 0x3F8
    mov  al, 'X'
    out  dx, al

    ; SYS_WRITE via int 0x80
    mov  eax, 1
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80

    jmp  $

msg:     db "Hello from ELF!", 0
msg_len  equ $ - msg
