; test_elf.asm — mouse paint: red dot follows cursor, click to change color

[bits 32]
[global _start]

_start:
    mov  eax, 9          ; SYS_GETFB
    mov  ebx, fbinfo
    int  0x80

    mov  edi, [fbinfo]    ; fb addr
    mov  esi, [fbinfo+4]  ; width
    ; height unused, full fb accessible

    mov  ebx, 0x00FF0000  ; current color = red

.loop:
    ; Get mouse position
    mov  eax, 11         ; SYS_MOUSE
    push ebx
    mov  ebx, mdata
    int  0x80
    pop  ebx

    mov  eax, [mdata]    ; mouse x
    mov  ecx, [mdata+4]  ; mouse y
    mov  edx, [mdata+8]  ; buttons

    ; Check bounds
    cmp  eax, 0
    jl   .wait
    cmp  ecx, 0
    jl   .wait
    cmp  eax, esi
    jge  .wait

    ; Button 1 (left click): cycle color
    test edx, 1
    jz   .draw
    ror  ebx, 8           ; shift color: red→green→blue→red

.draw:
    ; Draw 4x4 dot at mouse position
    push edx
    push eax
    push ecx
    push edi

    ; y * width * 4 + x * 4 — use width*4 as pitch
    shl  ecx, 2            ; y*4
    imul ecx, esi          ; y*4*width
    shl  eax, 2            ; x*4
    add  eax, ecx          ; offset = y*width*4 + x*4
    add  edi, eax          ; fb + offset

    ; 4x4 square
    mov  ecx, 4
    push esi
    shl  esi, 2            ; width * 4 = pitch
.yl:
    mov  eax, 4
.xl:
    mov  [edi], ebx
    add  edi, 4
    dec  eax
    jnz  .xl
    add  edi, esi
    sub  edi, 16           ; back to column start, next row
    dec  ecx
    jnz  .yl
    pop  esi

    pop  edi
    pop  ecx
    pop  eax

    ; Short delay for responsiveness
    mov  eax, 12          ; SYS_SLEEP
    push ebx
    mov  ebx, 10           ; 10ms
    int  0x80
    pop  ebx

.wait:
    ; Check for keyboard (ESC to exit)
    mov  eax, 12
    push ebx
    mov  ebx, 1            ; 1ms quick poll
    int  0x80
    pop  ebx
    jmp  .loop

section .bss
fbinfo: resd 5
mdata:  resd 3
