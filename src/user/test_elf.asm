; test_elf.asm — SYS_WRITE test

[bits 32]
[global _start]

_start:
    mov  eax, 1
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80
    jmp  $

msg:     db "Hello from ELF!", 0
msg_len  equ $ - msg
