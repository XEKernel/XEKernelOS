; ============================================================
; XEKernelOS - Stage 2: 最简保护模式加载器
;
; 什么都不画, 只做:
;   1. 屏蔽 PIC
;   2. 安装 GDT
;   3. 进入保护模式
;   4. 跳转到 C 内核 (0x20000)
; ============================================================

[org 0x0000]
[bits 16]

entry:
    mov ax, cs              ; CS=0x1000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE

    ; ---- Load kernel via extended INT 13h ----
    movzx ecx, word [cs:0x1FFE]  ; kernel sectors (written by build_img.py)
    test cx, cx
    jz  .skip_kernel

    ; DAP setup in stage2's data area
    mov byte [cs:dap_size], 0x10
    mov word [cs:dap_buf_seg], 0x2000
    mov dword [cs:dap_lba], 17     ; kernel starts at LBA 17
    mov word [cs:dap_buf_off], 0

    ; Extended INT 13h supports up to 127 sectors per call.
    ; Split large kernels into 127-sector chunks.
.kload:
    mov ax, cx
    cmp ax, 127
    jbe .last
    mov ax, 127
.last:
    mov [cs:dap_count], ax

    push cx
    mov ah, 0x42
    mov dl, 0x80
    mov si, dap
    int 0x13
    pop cx
    jc  boot_stop

    sub cx, ax              ; remaining sectors
    jz  .skip_kernel

    ; Advance LBA and buffer offset
    add [cs:dap_lba], eax
    shl ax, 9               ; ax * 512 → bytes
    add [cs:dap_buf_off], ax
    jmp .kload

.skip_kernel:

    ; VBE 模式 0x4144 (1024x768x32, QEMU/SeaBIOS)
    mov ax, 0x4F02
    mov bx, 0x4144
    or bx, 0x4000
    int 0x10

    mov di, mode_info
    mov ax, 0x4F01
    mov cx, 0x4144
    int 0x10

    push ds
    xor ax, ax
    mov ds, ax
    mov eax, [cs:mode_info + 40]
    mov [0x500], eax
    movzx eax, word [cs:mode_info + 18]
    mov [0x504], eax
    movzx eax, word [cs:mode_info + 20]
    mov [0x508], eax
    movzx eax, byte [cs:mode_info + 25]
    mov [0x50C], eax
    movzx eax, word [cs:mode_info + 16]
    mov [0x510], eax
    pop ds

    ; 禁 PIC
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    cli
    lgdt [cs:gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; ============================================================
; GDT: 4 entries (selector ×8)
;   0x08 = stage2 code (base=0x10000)
;   0x10 = flat data   (base=0)
;   0x18 = kernel code (base=0)
; ============================================================
gdt_start:
    dq 0                    ; null (0x00)

gdt_code_stage2:
    dw 0xFFFF               ; limit[15:0]
    dw 0x0000               ; base[15:0]
    db 0x01                 ; base[23:16] = 1  →  base = 0x00010000
    db 0x9A                 ; access: P=1, DPL=0, code, R=1
    db 0xCF                 ; flags: G=1, D/B=1, L=0, limit[19:16]=0xF
    db 0x00                 ; base[31:24] = 0

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                 ; access: P=1, DPL=0, data, W=1
    db 0xCF
    db 0x00

gdt_code_kernel:
    dw 0xFFFF               ; limit[15:0]
    dw 0x0000               ; base[15:0]
    db 0x00                 ; base[23:16] = 0  →  base = 0x00000000
    db 0x9A                 ; access: P=1, DPL=0, code, R=1
    db 0xCF                 ; flags: G=1, D/B=1, L=0, limit[19:16]=0xF
    db 0x00                 ; base[31:24] = 0

gdt_user_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92 | 0x60          ; access: DPL=3, data, W=1
    db 0xCF
    db 0x00

gdt_user_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A | 0x60          ; access: DPL=3, code, R=1
    db 0xCF
    db 0x00

gdt_tss:
    dw 0x0067               ; limit = 103
    dw 0x0000               ; base[15:0]  (filled at runtime)
    db 0x00                 ; base[23:16] (filled at runtime)
    db 0x89                 ; access: P=1, DPL=0, TSS available
    db 0x40                 ; flags: G=0, D/B=0
    db 0x00                 ; base[31:24] (filled at runtime)

gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start + 0x10000

; ============================================================
; 32 位保护模式入口
; ============================================================
[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FBFF

    ; 跳转到 C 内核 (物理 0x20000)
    jmp 0x18:0x20000

boot_stop:
    cli
    hlt
    jmp boot_stop

; Extended INT 13h Disk Address Packet (used by kernel loader)
dap:
dap_size:    db 0
             db 0
dap_count:   dw 0
dap_buf_off: dw 0
dap_buf_seg: dw 0
dap_lba:     dq 0

; VBE 缓冲区
vbe_info:  times 512 db 0
mode_info: times 256 db 0
mode_num:  dw 0

times 8192 - ($ - $$) db 0
