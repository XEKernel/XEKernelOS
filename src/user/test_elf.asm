; test_elf.asm — SYS_WRITE + SYS_EXIT

[bits 32]
[global _start]

_start:
    mov  eax, 1          ; SYS_WRITE
    mov  ebx, msg1
    mov  ecx, msg1_len
    int  0x80

    mov  eax, 1
    mov  ebx, msg2
    mov  ecx, msg2_len
    int  0x80

    mov  eax, 2          ; SYS_EXIT
    int  0x80

msg1:    db "Hello from ELF!", 0
msg1_len equ $ - msg1
msg2:    db "Goodbye!", 0
msg2_len equ $ - msg2
