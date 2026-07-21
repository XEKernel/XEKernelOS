; test_elf.asm — mouse crosshair, 16ms refresh

[bits 32]
[global _start]

_start:
    mov  eax, 13         ; SYS_CLS — black
    mov  ebx, 0
    int  0x80

    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80

    mov  edi, [fbinfo]    ; fb addr
    mov  esi, [fbinfo+4]  ; width

    ; precompute row stride = width * 4
    shl  esi, 2

    mov  ebp, 0x00FF0000  ; color: red

.loop:
    mov  eax, 11         ; SYS_MOUSE
    mov  ebx, mdata
    int  0x80

    mov  eax, [mdata]    ; x
    mov  ecx, [mdata+4]  ; y
    mov  edx, [mdata+8]  ; btn

    ; click → cycle color
    test edx, 1
    jz   .draw
    ror  ebp, 8

.draw:
    ; Draw horizontal line: y at mouse_y, full width
    imul ecx, esi          ; ecx = y * stride
    add  ecx, edi          ; ecx = fb + y*stride
    mov  eax, 0
    mov  edx, [fbinfo+4]   ; width
.hline:
    cmp  eax, edx
    jge  .vline
    mov  [ecx + eax*4], ebp
    inc  eax
    jmp  .hline

.vline:
    ; Draw vertical line: x at mouse_x, full height
    mov  ecx, [mdata]      ; x
    mov  eax, 0
    mov  edx, [fbinfo+8]   ; height
.vloop:
    cmp  eax, edx
    jge  .wait
    push eax
    imul eax, esi          ; y * stride
    add  eax, edi          ; fb + y*stride
    mov  [eax + ecx*4], ebp
    pop  eax
    inc  eax
    jmp  .vloop

.wait:
    mov  eax, 12          ; SYS_SLEEP 16ms ≈ 60fps
    mov  ebx, 16
    int  0x80
    jmp  .loop

section .bss
fbinfo: resd 5
mdata:  resd 3
