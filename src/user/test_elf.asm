; test_elf.asm — minimal ELF user program for XEKernelOS
; Build: nasm -f elf32 src/user/test_elf.asm -o build/test_elf.o
; Link:  ld.lld -m elf_i386 -Ttext=0x400000 build/test_elf.o -o build/test_elf.elf
;
; Usage in shell: RUN TEST_ELF.ELF

[bits 32]
[global _start]

_start:
    ; SYS_WRITE(1): eax=1, ebx=str, ecx=len
    mov  eax, 1
    mov  ebx, msg
    mov  ecx, msg_len
    int  0x80

    ; Halt: infinite loop
    jmp  $

msg:     db "Hello from ELF user program!", 0
msg_len  equ $ - msg
