; ============================================================
; XEKernelOS User Program - Hello World (Ring 3)
; Compile: nasm -f bin -o hello.bin hello.asm
; Load:   RUN HELLO.BIN
; Output: QEMU serial console (run.py has -serial stdio)
; ============================================================

[bits 32]
[org 0x400000]

entry:
    ; ---- SYS_WRITE (eax=1): ebx=str, ecx=len ----
    mov eax, 1
    mov ebx, msg1
    mov ecx, msg1_len
    int 0x80

    mov eax, 1
    mov ebx, msg2
    mov ecx, msg2_len
    int 0x80

    mov eax, 1
    mov ebx, msg3
    mov ecx, msg3_len
    int 0x80

    mov eax, 1
    mov ebx, msg4
    mov ecx, msg4_len
    int 0x80

    ; Exit back to shell
    mov eax, 2
    int 0x80

msg1: db "====================================", 0
msg1_len equ $ - msg1 - 1
msg2: db " Hello from XEKernelOS User Mode!  ", 0
msg2_len equ $ - msg2 - 1
msg3: db " Ring 3 program at 0x400000        ", 0
msg3_len equ $ - msg3 - 1
msg4: db "====================================", 0
msg4_len equ $ - msg4 - 1
