; XEKernelOS Bouncing Ball Demo — Phase 4b
; Simple single ball bouncing
bits 32
org 0x400000

start:
    ; Fill screen blue
    mov eax, 46           ; SYS_IOCTL
    mov ebx, 0            ; fb fd
    mov ecx, 3            ; IO_FILL
    mov edx, bg           ; {0,0,640,480,blue}
    int 0x80

    ; Title text
    mov eax, 46
    mov ebx, 0
    mov ecx, 7            ; IO_TEXT
    mov edx, title
    int 0x80

    ; Init ball position and velocity
    mov dword [ball_x], 100
    mov dword [ball_y], 100
    mov dword [vel_x], 3
    mov dword [vel_y], 2
    mov esi, 400          ; ~8 seconds then exit

.loop:
    ; Erase old ball
    mov eax, 46
    mov ebx, 0
    mov ecx, 3
    mov edx, eraser
    int 0x80

    ; Update X
    mov eax, [ball_x]
    add eax, [vel_x]
    cmp eax, 20
    jl .bxl
    cmp eax, 612
    jg .bxr
    jmp .cy
.bxl: neg dword [vel_x]; mov eax, 20; jmp .cy
.bxr: neg dword [vel_x]; mov eax, 612

.cy:
    mov [ball_x], eax
    mov [ball], ax       ; update ball rect x

    ; Update Y
    mov eax, [ball_y]
    add eax, [vel_y]
    cmp eax, 60
    jl .byd
    cmp eax, 452
    jg .byu
    jmp .draw
.byd: neg dword [vel_y]; mov eax, 60; jmp .draw
.byu: neg dword [vel_y]; mov eax, 452

.draw:
    mov [ball_y], eax
    mov [ball+2], ax     ; update ball rect y

    ; Draw ball
    mov eax, 46
    mov ebx, 0
    mov ecx, 3
    mov edx, ball
    int 0x80

    ; Sleep 25ms
    mov eax, 12           ; SYS_SLEEP
    mov ebx, 25
    int 0x80

    dec esi
    jnz .loop

    ; Exit
    mov eax, 2            ; SYS_EXIT
    int 0x80

section .data
bg:     dw 0, 0, 640, 480
        db 0x09              ; blue

title:  dw 20, 10
        db 0x0F              ; white (u8, not u16!)
        db "  XEKernelOS Bouncing Ball  ", 0
times 69 - ($-title) db 0

eraser: dw 0, 0, 10, 10, 0x09            ; blue rect to erase
ball:   dw 0, 0, 10, 10, 0x0E            ; yellow ball rect

ball_x: dd 100
ball_y: dd 100
vel_x:  dd 3
vel_y:  dd 2
