; test_elf.asm — argc + SYS_GETCWD + SYS_TIME demo

[bits 32]
[global _start]

_start:
    pop  eax                ; argc

    ; SYS_GETCWD
    push eax
    mov  eax, 7
    mov  ebx, buf
    mov  ecx, 64
    int  0x80
    mov  eax, 1
    mov  ebx, buf
    mov  ecx, 64
    int  0x80
    mov  eax, 1
    mov  ebx, nl
    mov  ecx, 1
    int  0x80

    ; SYS_TIME
    mov  eax, 8
    mov  ebx, buf
    int  0x80
    mov  eax, 1
    mov  ebx, buf
    mov  ecx, 8
    int  0x80
    mov  eax, 1
    mov  ebx, nl
    mov  ecx, 1
    int  0x80

    pop  eax
    mov  eax, 2
    int  0x80

nl:     db 0x0A

section .bss
buf:   resb 64
