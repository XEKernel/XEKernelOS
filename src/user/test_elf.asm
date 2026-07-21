; test_elf.asm — minimal ELF user program for XEKernelOS
; Build: nasm -f elf32 src/user/test_elf.asm -o build/test_elf.o
; Link:  ld.lld -m elf_i386 -Ttext=0x08000000 build/test_elf.o -o build/test_elf.elf

[bits 32]
[global _start]

_start:
    ; Debug: direct serial out (bypass int 0x80)
    mov  dx, 0x3F8
    mov  al, 'X'
    out  dx, al

    ; SYS_WRITE(1): eax=1, ebx=str, ecx=len
    mov  eax, 1
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80

    ; Halt
    jmp  $

msg:     db "Hello from ELF user program!", 0
msg_len  equ $ - msg
