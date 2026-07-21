; test_elf.asm — framebuffer: red full screen using SYS_GETFB dimensions

[bits 32]
[global _start]

_start:
    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80
    cmp  eax, 5
    jne  .done

    mov  edi, [fbinfo]    ; fb addr
    mov  esi, [fbinfo+4]  ; width
    mov  ebp, [fbinfo+8]  ; height
    mov  ebx, [fbinfo+12] ; pitch (bytes per line)

    ; Line-by-line fill — use actual pitch
    xor  edx, edx          ; y = 0
.yloop:
    cmp  edx, ebp
    jge  .done

    push edx
    ; Row offset = y * pitch
    mov  eax, edx
    mul  ebx               ; eax = y * pitch
    add  eax, edi          ; eax = fb + y * pitch

    ; Fill one row red (BGRA: R=255→byte2=0xFF)
    xor  ecx, ecx          ; x = 0
.xloop:
    cmp  ecx, esi
    jge  .nexty
    ; Pixel offset = x * 4 (32bpp) — but use actual bpp
    push eax
    ; Use 4 bytes per pixel (safe even at 16bpp, just overwrites next pixel)
    lea  eax, [eax + ecx*4]
    mov  dword [eax], 0x0000FF00  ; Red: BGRA → byte2=FF, rest=00
    pop  eax
    inc  ecx
    jmp  .xloop
.nexty:
    pop  edx
    inc  edx
    jmp  .yloop
.done:
    mov  eax, 2
    int  0x80

section .bss
fbinfo: resd 5
