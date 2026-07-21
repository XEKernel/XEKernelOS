; test_elf.asm — SYS_WRITE + SYS_READ + SYS_EXIT demo

[bits 32]
[global _start]

_start:
    ; Prompt
    mov  eax, 1
    mov  ebx, prompt
    mov  ecx, prompt_len
    int  0x80

    ; Read name
    mov  eax, 3          ; SYS_READ
    mov  ebx, buf
    mov  ecx, 32
    int  0x80

    ; Greeting
    mov  eax, 1
    mov  ebx, hello
    mov  ecx, hello_len
    int  0x80

    ; Echo name
    mov  eax, 1
    mov  ebx, buf
    mov  ecx, 32
    int  0x80

    ; Bye
    mov  eax, 1
    mov  ebx, bye
    mov  ecx, bye_len
    int  0x80

    mov  eax, 2          ; SYS_EXIT
    int  0x80

prompt:    db "What's your name?", 0
prompt_len equ $ - prompt
hello:     db "Hello, ", 0
hello_len  equ $ - hello
bye:       db "! Goodbye!", 0
bye_len    equ $ - bye

section .bss
buf:   resb 32
