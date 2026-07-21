; XEKernelOS MBR - load stage2 via extended INT 13h, kernel loaded by stage2
[org 0x7c00]
[bits 16]

start:
    jmp short real_start
    nop
bpb: times 33 db 0

real_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Check extended INT 13h support
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, 0x80
    int 0x13
    jc  _err
    cmp bx, 0xAA55
    jne _err

    ; Load stage2 (LBA 1, 16 sectors) → 0x10000
    mov dword [dap_lba], 1
    mov word [dap_count], 16
    mov word [dap_buf_seg], 0x1000
    mov word [dap_buf_off], 0
    mov [dap_size], byte 0x10
    mov si, dap
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc  _err

    jmp 0x1000:0x0000

_err:
    mov si, _msg
    mov ah, 0x0E
_lp:lodsb
    test al, al
    jz _hlt
    int 0x10
    jmp _lp
_hlt:cli
    hlt
    jmp _hlt

_msg: db "ERR: Disk!", 0

dap:
dap_size:    db 0
             db 0
dap_count:   dw 0
dap_buf_off: dw 0
dap_buf_seg: dw 0
dap_lba:     dq 0

times 510 - ($ - $$) db 0
dw 0xAA55
