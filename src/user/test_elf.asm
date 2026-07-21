; test_elf.asm — mouse cursor square + ESC to exit

[bits 32]
[global _start]

_start:
    mov  eax, 13         ; SYS_CLS black
    mov  ebx, 0
    int  0x80

    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80

    mov  edi, [fbinfo]    ; fb addr
    mov  esi, [fbinfo+4]  ; width
    shl  esi, 2           ; stride = width * 4 (BGRA)

    mov  ebp, 0x00FF0000  ; color: red

.loop:
    ; Save previous fb state? Skip for now — just draw
    mov  eax, 13         ; cls black to erase previous frame
    mov  ebx, 0
    int  0x80

    mov  eax, 11         ; SYS_MOUSE
    mov  ebx, mdata
    int  0x80

    mov  eax, [mdata]    ; x
    mov  ecx, [mdata+4]  ; y
    mov  edx, [mdata+8]  ; btn

    ; click → color cycle
    test edx, 1
    jz   .draw
    ror  ebp, 8

.draw:
    ; Draw 32x32 cursor box at (x, y)
    ; First sanitize: clamp to screen
    cmp  eax, 0
    jge  .xok
    xor  eax, eax
.xok:
    cmp  eax, [fbinfo+4]
    jl   .xok2
    mov  eax, [fbinfo+4]
    sub  eax, 32
.xok2:
    cmp  ecx, 0
    jge  .yok
    xor  ecx, ecx
.yok:
    cmp  ecx, [fbinfo+8]
    jl   .yok2
    mov  ecx, [fbinfo+8]
    sub  ecx, 32
.yok2:

    ; Compute base = fb + y*stride + x*4
    push eax               ; save x
    imul ecx, esi          ; y * stride
    add  ecx, edi          ; fb + y*stride
    pop  eax
    lea  ecx, [ecx + eax*4]

    ; Fill 32x32
    mov  edx, 32
.yl:
    mov  eax, 32
    push ecx
.xl:
    mov  [ecx], ebp
    add  ecx, 4
    dec  eax
    jnz  .xl
    pop  ecx
    add  ecx, esi
    dec  edx
    jnz  .yl

    ; 30ms delay (~33fps)
    mov  eax, 12
    mov  ebx, 30
    int  0x80
    jmp  .loop

section .bss
fbinfo: resd 5
mdata:  resd 3
