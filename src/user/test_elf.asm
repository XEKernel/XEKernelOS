; test_elf.asm — fill screen red, then green stripe

[bits 32]
[global _start]

_start:
    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80
    cmp  eax, 5
    jne  .done

    mov  edi, [fbinfo]    ; fb addr

    ; Fill entire screen red (1024 * 768 pixels * 4 bytes)
    mov  ecx, 1024*768/4  ; dwords per screen (assume 32bpp)
    mov  eax, 0x000000FF  ; red (B=255, G=0, R=0 → reversed in little-endian)
.fill:
    mov  dword [edi], eax
    add  edi, 4
    dec  ecx
    jnz  .fill

    ; Wait for keypress
    mov  eax, 3
    mov  ebx, buf
    mov  ecx, 2
    int  0x80

    ; Now draw green stripe in middle
    mov  edi, [fbinfo]
    add  edi, 768/2 * 1024 * 4   ; halfway down
    mov  ecx, 1024 * 100         ; 100 rows
.green:
    mov  dword [edi], 0x0000FF00
    add  edi, 4
    dec  ecx
    jnz  .green

    mov  eax, 3
    mov  ebx, buf
    mov  ecx, 2
    int  0x80

.done:
    mov  eax, 2
    int  0x80

section .bss
fbinfo: resd 5
buf:    resb 2
