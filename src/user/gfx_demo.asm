; XEKernelOS 图形演示 — 使用 SYS_IOCTL 绘制基本图形
; Phase 4b: 图形增强
bits 32
org 0x400000

start:
    ; 清屏为深蓝色
    mov eax, 46         ; SYS_IOCTL
    mov ebx, 0          ; fd0 = framebuffer
    mov ecx, 2          ; IOCTL_GFX_CLS
    mov edx, 0x01       ; 蓝色
    int 0x80

    ; === 填充矩形演示 ===
    ; 大红色填充矩形 (左上)
    mov eax, 46
    mov ebx, 0
    mov ecx, 3          ; IOCTL_GFX_FILL
    mov edx, fill_red
    int 0x80

    ; 绿色填充矩形 (右上)
    mov eax, 46
    mov ebx, 0
    mov ecx, 3
    mov edx, fill_green
    int 0x80

    ; 青色填充矩形 (左下)
    mov eax, 46
    mov ebx, 0
    mov ecx, 3
    mov edx, fill_cyan
    int 0x80

    ; 黄色填充矩形 (右下)
    mov eax, 46
    mov ebx, 0
    mov ecx, 3
    mov edx, fill_yellow
    int 0x80

    ; === 矩形边框演示 ===
    ; 白色边框
    mov eax, 46
    mov ebx, 0
    mov ecx, 5          ; IOCTL_GFX_RECT
    mov edx, rect_white
    int 0x80

    ; 红色细边框
    mov eax, 46
    mov ebx, 0
    mov ecx, 5
    mov edx, rect_red
    int 0x80

    ; === 线条演示 ===
    ; 左上到右下对角线 (白色)
    mov eax, 46
    mov ebx, 0
    mov ecx, 4          ; IOCTL_GFX_LINE
    mov edx, line_diag
    int 0x80

    ; 右上到左下对角线 (黄色)
    mov eax, 46
    mov ebx, 0
    mov ecx, 4
    mov edx, line_diag2
    int 0x80

    ; 水平线 (品红)
    mov eax, 46
    mov ebx, 0
    mov ecx, 4
    mov edx, line_h
    int 0x80

    ; 垂直线 (浅绿)
    mov eax, 46
    mov ebx, 0
    mov ecx, 4
    mov edx, line_v
    int 0x80

    ; === 像素点阵 ===
    mov eax, 46
    mov ebx, 0
    mov ecx, 6          ; IOCTL_GFX_PIXEL
    mov edx, pixel_data
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80
    add edx, 5
    int 0x80

    ; 等待 2 秒 (方便观看)
    mov eax, 12         ; SYS_SLEEP
    mov ebx, 2000       ; 2000ms
    int 0x80

    ; 退出
    mov eax, 2          ; SYS_EXIT
    int 0x80

; === 数据段: IOCTL 参数结构 ===
; IOCTL_GFX_FILL: {i16 x, i16 y, i16 w, i16 h; u8 color} - 9 bytes packed
; IOCTL_GFX_RECT: 同上
; IOCTL_GFX_LINE: {i16 x1, i16 y1, i16 x2, i16 y2; u8 color} - 9 bytes packed
; IOCTL_GFX_PIXEL: {i16 x, i16 y; u8 color} - 5 bytes packed

section .data
fill_red:    dw 10, 30, 140, 100, 0x04    ; 红色 04
fill_green:  dw 170, 30, 140, 100, 0x0A   ; 浅绿 0A
fill_cyan:   dw 10, 150, 140, 100, 0x0B   ; 浅青 0B
fill_yellow: dw 170, 150, 140, 100, 0x0E  ; 黄色 0E

rect_white:  dw 5, 25, 310, 210, 0x0F     ; 白色边框 0F
rect_red:    dw 15, 35, 290, 190, 0x0C    ; 浅红边框 0C

line_diag:   dw 20, 45, 300, 235, 0x0F    ; 左上→右下 白色
line_diag2:  dw 300, 45, 20, 235, 0x0E    ; 右上→左下 黄色
line_h:      dw 20, 140, 300, 140, 0x0D   ; 水平 品红
line_v:      dw 160, 20, 160, 260, 0x0A   ; 垂直 浅绿

pixel_data:  dw 155, 135, 0x0F   ; 中心白点
             dw 160, 135, 0x0F
             dw 165, 135, 0x0F
             dw 155, 140, 0x0F
             dw 160, 140, 0x0F
             dw 165, 140, 0x0F
             dw 155, 145, 0x0F
             dw 160, 145, 0x0F
             dw 165, 145, 0x0F   ; 3x3 白色方块
