; test_elf.asm — minimal framebuffer test: one red pixel at (0,0)

[bits 32]
[global _start]

_start:
    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80
    cmp  eax, 5
    jne  .done

    ; Write red pixel at (0,0): B=0 G=0 R=255
    mov  esi, [fbinfo]
    mov  dword [esi], 0x000000FF

    ; Also write at (100,100) via pitch
    mov  edx, [fbinfo+12]  ; pitch
    imul edx, 100
    add  edx, esi
    add  edx, 400          ; 100 pixels * 4 bytes
    mov  dword [edx], 0x0000FF00  ; green

    ; And a blue line across top
    mov  ecx, 200
.topline:
    mov  dword [esi+ecx*4], 0x00FF0000  ; blue
    dec  ecx
    jnz  .topline

    ; Wait for keypress (SYS_READ), then exit
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
