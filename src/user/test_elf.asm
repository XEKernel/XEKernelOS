; test_elf.asm — SYS_WRITE + SYS_EXIT

[bits 32]
[global _start]

_start:
    mov  eax, 1          ; SYS_WRITE
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80

    mov  eax, 2          ; SYS_EXIT
    int  0x80

msg:     db "Hello from ELF!", 0
msg_len  equ $ - msg
