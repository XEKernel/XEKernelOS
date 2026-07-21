; test_elf.asm — SYS_SBRK demo

[bits 32]
[global _start]

_start:
    ; Allocate 32 bytes
    mov  eax, 6          ; SYS_SBRK
    mov  ebx, 32
    int  0x80
    cmp  eax, -1
    je   .fail

    mov  esi, eax        ; heap ptr

    ; Write to heap: "Hello from heap!"
    mov  byte [esi+0],  'H'
    mov  byte [esi+1],  'e'
    mov  byte [esi+2],  'l'
    mov  byte [esi+3],  'l'
    mov  byte [esi+4],  'o'
    mov  byte [esi+5],  ' '
    mov  byte [esi+6],  'f'
    mov  byte [esi+7],  'r'
    mov  byte [esi+8],  'o'
    mov  byte [esi+9],  'm'
    mov  byte [esi+10], ' '
    mov  byte [esi+11], 'h'
    mov  byte [esi+12], 'e'
    mov  byte [esi+13], 'a'
    mov  byte [esi+14], 'p'
    mov  byte [esi+15], '!'
    mov  byte [esi+16], 0

    ; Print it
    mov  eax, 1
    mov  ebx, esi
    mov  ecx, 16
    int  0x80

    ; Allocate more
    mov  eax, 6
    mov  ebx, 4096
    int  0x80
    cmp  eax, -1
    je   .fail

    ; Print success message
    mov  eax, 1
    mov  ebx, okmsg
    mov  ecx, okmsg_len
    int  0x80

    mov  eax, 2
    int  0x80

.fail:
    mov  eax, 1
    mov  ebx, errmsg
    mov  ecx, errmsg_len
    int  0x80
    mov  eax, 2
    int  0x80

okmsg:     db "SYS_SBRK OK!", 0
okmsg_len  equ $ - okmsg
errmsg:    db "SYS_SBRK failed!", 0
errmsg_len equ $ - errmsg
