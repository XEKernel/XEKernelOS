; test_elf.asm — framebuffer demo: color bars

[bits 32]
[global _start]

_start:
    ; SYS_GETFB → buf[0..4]
    mov  eax, 9
    mov  ebx, fbinfo
    int  0x80
    cmp  eax, 5
    jne  .done

    mov  esi, [fbinfo]     ; fb addr
    mov  eax, [fbinfo+4]   ; width
    mov  ebx, [fbinfo+8]   ; height
    mov  edx, [fbinfo+12]  ; pitch
    mov  ecx, [fbinfo+16]  ; bpp

    ; Draw color bars (8 strips)
    xor  edi, edi           ; y = 0
.yloop:
    cmp  edi, ebx
    jge  .done
    xor  ebp, ebp           ; x = 0
    mov  esi, [fbinfo]      ; reset fb ptr each row
.xloop:
    cmp  ebp, eax
    jge  .nexty

    ; Color = (x * 8 / width)
    push eax
    mov  eax, ebp
    shl  eax, 3             ; x * 8
    xor  edx, edx
    div  dword [fbinfo+4]   ; / width → al = strip index (0-7)
    pop  eax                ; restore width

    ; Map strip → RGB color
    ; 0=red(FF0000), 1=orange(FF7F00), 2=yellow(FFFF00), 3=green(00FF00),
    ; 4=cyan(00FFFF), 5=blue(0000FF), 6=magenta(FF00FF), 7=white(FFFFFF)
    cmp  al, 0
    jne  .c1
    mov  dword [esi+ebp*4], 0x00FF0000
    jmp  .nx
.c1:cmp  al, 1
    jne  .c2
    mov  dword [esi+ebp*4], 0x00007FFF
    jmp  .nx
.c2:cmp  al, 2
    jne  .c3
    mov  dword [esi+ebp*4], 0x0000FFFF
    jmp  .nx
.c3:cmp  al, 3
    jne  .c4
    mov  dword [esi+ebp*4], 0x0000FF00
    jmp  .nx
.c4:cmp  al, 4
    jne  .c5
    mov  dword [esi+ebp*4], 0x00FFFF00
    jmp  .nx
.c5:cmp  al, 5
    jne  .c6
    mov  dword [esi+ebp*4], 0x00FF0000
    jmp  .nx
.c6:cmp  al, 6
    jne  .c7
    mov  dword [esi+ebp*4], 0x00FF00FF
    jmp  .nx
.c7:
    mov  dword [esi+ebp*4], 0x00FFFFFF
.nx:
    inc  ebp
    jmp  .xloop
.nexty:
    inc  edi
    jmp  .yloop
.done:
    mov  eax, 2
    int  0x80

section .bss
fbinfo: resd 5    ; {fb_addr, w, h, pitch, bpp}
