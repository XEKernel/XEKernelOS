; Minimal GFX test — fill screen red + exit
bits 32
org 0x400000

start:
    mov eax, 46         ; SYS_IOCTL
    mov ebx, 0          ; fd 0 = framebuffer
    mov ecx, 3          ; IOCTL_GFX_FILL
    mov edx, rect       ; {0,0,640,200,RED}
    int 0x80

    mov eax, 12         ; SYS_SLEEP
    mov ebx, 1000       ; 1 second
    int 0x80

    mov eax, 2          ; SYS_EXIT
    int 0x80

section .data
rect: dw 0, 0, 640, 200
      db 0x04           ; red
