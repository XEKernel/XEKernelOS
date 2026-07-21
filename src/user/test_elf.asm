; test_elf.asm — minimal syscall test

[bits 32]
[global _start]

_start:
    ; SYS_WRITE(1): eax=1, ebx=str, ecx=len
    mov  eax, 1
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80

    jmp  $

msg:     db "Hello from ELF!", 0
msg_len  equ $ - msg
