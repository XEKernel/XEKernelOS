; XEKernelOS MBR - Stage 1: load stage2 + kernel to RAM
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

    mov ah, 0x00
    mov dl, 0x00
    int 0x13

; load stage2 (LBA 1-16) to 0x10000
    mov ah, 0x02
    mov al, 16
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x00
    push 0x1000
    pop es
    mov bx, 0
    int 0x13
    jc _err

; load kernel part1 (LBA 17-34, 18 sectors) to 0x20000
    mov ah, 0x02
    mov al, 18
    mov ch, 0
    mov cl, 18
    mov dh, 0
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0
    int 0x13
    jc _err

; load kernel part1b (LBA 35, 1 sector) to 0x22400
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 18
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x2400
    int 0x13
    jc _err

; load kernel part2 (LBA 36-53, 18 sectors) to 0x22600
    mov ah, 0x02
    mov al, 18
    mov ch, 1
    mov cl, 1
    mov dh, 0
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x2600
    int 0x13
    jc _err

; load kernel part3 (LBA 54-57, 4 sectors) to 0x24A00
    mov ah, 0x02
    mov al, 4
    mov ch, 1
    mov cl, 1
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x4A00
    int 0x13
    jc _err

; load kernel part4 (LBA 58-62, 5 sectors) to 0x25200
    mov ah, 0x02
    mov al, 5
    mov ch, 1
    mov cl, 5
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x5200
    int 0x13
    jc _err

; load kernel part5 (LBA 63-64, 2 sectors) to 0x25C00
    mov ah, 0x02
    mov al, 2
    mov ch, 1
    mov cl, 10
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x5C00
    int 0x13
    jc _err

; load kernel part6 (LBA 65-69, 5 sectors) to 0x26000
    mov ah, 0x02
    mov al, 5
    mov ch, 1
    mov cl, 12
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x6000
    int 0x13
    jc _err

; load kernel part7 (LBA 70-74, 5 sectors) to 0x26A00
    mov ah, 0x02
    mov al, 5
    mov ch, 1
    mov cl, 17
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x6A00
    int 0x13
    jc _err

; load kernel part8 (LBA 75-85, 11 sectors) to 0x27400
    mov ah, 0x02
    mov al, 11
    mov ch, 2
    mov cl, 4
    mov dh, 0
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x7400
    int 0x13
    jc _err

; load kernel part9 (LBA 86, 1 sector) to 0x28A00
    mov ah, 0x02
    mov al, 1
    mov ch, 2
    mov cl, 15
    mov dh, 0
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x8A00
    int 0x13
    jc _err

; load kernel part10 (LBA 87-100, 14 sectors) to 0x28C00
    mov ah, 0x02
    mov al, 14
    mov ch, 2
    mov cl, 16
    mov dh, 0
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0x8C00
    int 0x13
    jc _err

; load kernel part11 (LBA 101-106, 6 sectors) to 0x2A800
    mov ah, 0x02
    mov al, 6
    mov ch, 2
    mov cl, 12
    mov dh, 1
    mov dl, 0x00
    push 0x2000
    pop es
    mov bx, 0xA800
    int 0x13
    jc _err

; jump to stage2
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

times 510 - ($ - $$) db 0
dw 0xAA55
