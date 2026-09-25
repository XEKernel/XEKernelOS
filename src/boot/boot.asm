; XEKernelOS MBR - load stage2 via extended INT 13h, kernel loaded by stage2
;
; 磁盘布局 (引导代码不依赖分区表, 仍按 LBA 直读):
;   LBA 0       本引导扇区 (含一张"活动分区"表项, 见文末)
;   LBA 1..16   stage2  (16 扇区, 加载到 0x10000)
;   LBA 17..    内核    (扇区数由 build_img.py 写进 stage2 偏移 0x1FFE)
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

    ; BIOS 跳到 MBR 时 DL = 启动盘号 (0x80 = 第一块硬盘)。
    ; 旧实现硬编码 0x80 读盘 — 在双盘/顺序不同的机器上会读错盘。
    mov [boot_drv], dl

    ; 早期可见标记: 证明 BIOS 确实执行了我们的引导扇区。
    ; VMware 上"BIOS 画面后只剩光标闪烁"时, 有没有这行就是
    ; "BIOS 没引导我们" 与 "引导了但后续卡住" 的分水岭。
    mov si, _msg_mbr
    call _puts

    ; 复位磁盘控制器: 个别 BIOS/虚拟机交棒后控制器仍处于忙状态, 不先
    ; reset 直接发扩展读会返回 CF=1 (真实硬件上的经典问题)
    mov ah, 0x00
    mov dl, [boot_drv]
    int 0x13

    ; Check extended INT 13h support
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drv]
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
    mov dl, [boot_drv]              ; BIOS 可能破坏 DL, 每次调用前重载
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc  _err

    ; 跳转前把启动盘号放进 DL — stage2 也用它读内核 (它无法自己得知)
    mov dl, [boot_drv]
    jmp 0x1000:0x0000

_err:
    mov si, _msg_err
    call _puts
_hlt:
    cli
    hlt
    jmp _hlt

; SI = 0 结尾字符串 (DS:SI), 电传输出
_puts:
    push ax
    mov ah, 0x0E
_puts_lp:
    lodsb
    test al, al
    jz _puts_end
    int 0x10
    jmp _puts_lp
_puts_end:
    pop ax
    ret

_msg_mbr: db "XEKernelOS MBR", 13, 10, 0
_msg_err: db "ERR: Disk!", 13, 10, 0

boot_drv: db 0x80

dap:
dap_size:    db 0
             db 0
dap_count:   dw 0
dap_buf_off: dw 0
dap_buf_seg: dw 0
dap_lba:     dq 0

; ---- 分区表 (必须有活动项) ----
; 部分 BIOS —— 含 VMware 的 Phoenix BIOS —— 对"分区表全 0"的磁盘直接
; 判定为不可引导, 表现为 BIOS 画面后只剩光标闪烁 (SeaBIOS 容忍, 所以
; QEMU 上一直没暴露)。本引导代码不读分区表, 这一项只为让 BIOS 放行。
; CHS 填 0xFE/0xFF/0xFF 表示"按 LBA 解释"。
    times 446 - ($ - $$) db 0
part_entry:
    db 0x80                     ; 活动标志
    db 0xFE, 0xFF, 0xFF         ; 起始 CHS (无效 → 用 LBA)
    db 0x01                     ; 类型: FAT12
    db 0xFE, 0xFF, 0xFF         ; 结束 CHS
    dd 1                        ; 起始 LBA = 1
    dd 2879                     ; 扇区数 (1.44MB 镜像去掉引导扇区)

    times 510 - ($ - $$) db 0
    dw 0xAA55
