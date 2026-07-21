; test_elf.asm — file I/O demo: cat README.TXT

[bits 32]
[global _start]

_start:
    ; SYS_OPEN("README  TXT")  — FAT12 8.3 name, space-padded
    mov  eax, 4
    mov  ebx, fname
    int  0x80
    cmp  eax, -1
    je   .fail

    ; SYS_FREAD(fd=0, buf, 512)
    mov  eax, 5
    mov  ebx, 0
    mov  ecx, buf
    mov  edx, 512
    int  0x80

    ; SYS_WRITE the file content
    mov  ecx, eax          ; bytes read
    mov  eax, 1
    mov  ebx, buf
    int  0x80

    mov  eax, 2            ; SYS_EXIT
    int  0x80

.fail:
    mov  eax, 1
    mov  ebx, errmsg
    mov  ecx, errmsg_len
    int  0x80
    mov  eax, 2
    int  0x80

fname:      db "README.TXT", 0    ; str_to_name83 splits on '.'
errmsg:     db "File not found!", 0
errmsg_len  equ $ - errmsg

section .bss
buf:   resb 512
