/* XEKernelOS 桌面环境 (Phase 4b: 图形增强)
   - 输入: /dev/input 统一事件流 (准则一: input 也是 fd)
   - 绘制: framebuffer fd 的 ioctl (FILL/RECT/TEXT)
   - 启动程序: 点击图标 → fork + exec_fd (全屏运行, 退出回桌面)
   - 退出: ESC → 回到用户 Shell
   鼠标指针由内核 PIT 的 mcursor_update 绘制 (XOR), 本程序无需处理。 */

typedef unsigned int  u32;
typedef int           i32;

/* ---- syscall numbers (与 kernel/syscall.h 一致) ---- */
#define SYS_EXIT      2
#define SYS_READ      3
#define SYS_OPEN      4
#define SYS_FREAD     5
#define SYS_TIME      8
#define SYS_GETFB     9
#define SYS_CLOSE    10
#define SYS_SLEEP    12
#define SYS_FORK     24
#define SYS_WAITPID  26
#define SYS_IOCTL    46
#define SYS_EXEC_FD  47

#define IOCTL_FILL    3
#define IOCTL_RECT    5
#define IOCTL_TEXT    7

#define FB_FD 0

/* 调色板索引 */
#define C_BLACK  0x00
#define C_BLUE   0x01
#define C_LBLUE  0x09
#define C_LGRAY  0x07
#define C_WHITE  0x0F
#define C_RED    0x04
#define C_LRED   0x0C
#define C_YELLOW 0x0E
#define C_LGREEN 0x0A

static inline int _sys3(int n, int a, int b, int c) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static inline int _sys1(int n, int a) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory");
    return r;
}

static void fill(int x, int y, int w, int h, u32 c) {
    /* {i16 x,y,w,h; u8 color} packed */
    struct { short x, y, w, h; unsigned char c; } __attribute__((packed)) p;
    p.x = (short)x; p.y = (short)y; p.w = (short)w; p.h = (short)h; p.c = c;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_FILL, (int)&p);
}
static void rect(int x, int y, int w, int h, u32 c) {
    struct { short x, y, w, h; unsigned char c; } __attribute__((packed)) p;
    p.x = (short)x; p.y = (short)y; p.w = (short)w; p.h = (short)h; p.c = c;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_RECT, (int)&p);
}
static void text(int x, int y, const char *s, u32 c) {
    struct { short x, y; unsigned char c; char t[56]; } __attribute__((packed)) p;
    p.x = (short)x; p.y = (short)y; p.c = c;
    int i = 0;
    while (s[i] && i < 55) { p.t[i] = s[i]; i++; }
    p.t[i] = 0;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_TEXT, (int)&p);
}

/* ---- 桌面布局 ---- */
static int g_w = 1024, g_h = 768;
#define TASKBAR_H 28

struct icon_def { const char *file; const char *label; int kind; };
static const icon_def ICONS[] = {
    { "USHELL.BIN",  "Terminal", 3 },
    { "GFXDEMO.BIN", "GFX Demo", 0 },
    { "BOUNCE.BIN",  "Bounce",   1 },
    { "DEMO.BIN",    "Demo",     2 },
};
#define N_ICONS (int)(sizeof(ICONS) / sizeof(ICONS[0]))

#define ICON_SZ 48
static int icon_x(int i) { return 60 + i * 110; }
static int icon_y(void)  { return 80; }

static void draw_icon(int i) {
    int x = icon_x(i), y = icon_y();
    const icon_def *d = &ICONS[i];

    fill(x, y, ICON_SZ, ICON_SZ, C_BLUE);
    rect(x - 2, y - 2, ICON_SZ + 4, ICON_SZ + 4, C_WHITE);

    if (d->kind == 0) {          /* GFX Demo: 三色拼图 */
        fill(x + 8,  y + 8,  12, 32, C_LRED);
        fill(x + 20, y + 8,  8,  32, C_LGREEN);
        fill(x + 28, y + 8,  12, 32, C_YELLOW);
    } else if (d->kind == 1) {   /* Bounce: 黄球 */
        fill(x + 14, y + 10, 20, 28, C_YELLOW);
        fill(x + 10, y + 16, 28, 16, C_YELLOW);
        fill(x + 18, y + 8,  12, 32, C_YELLOW);
    } else if (d->kind == 3) {   /* Terminal: 黑窗 + 白色 >_ 提示符 */
        fill(x + 8, y + 8, 32, 32, C_BLACK);
        rect(x + 8, y + 8, 32, 32, C_WHITE);
        fill(x + 14, y + 14, 5, 5, C_WHITE);   /* > 上撇 */
        fill(x + 19, y + 19, 5, 5, C_WHITE);   /* > 尖 */
        fill(x + 14, y + 24, 5, 5, C_WHITE);   /* > 下撇 */
        fill(x + 26, y + 30, 10, 4, C_WHITE);  /* _ 光标 */
    } else {                     /* Demo: 白色方块 + 点 */
        fill(x + 10, y + 10, 28, 28, C_LGRAY);
        fill(x + 16, y + 16, 6, 6, C_RED);
        fill(x + 26, y + 16, 6, 6, C_BLUE);
        fill(x + 16, y + 26, 6, 6, C_LGREEN);
        fill(x + 26, y + 26, 6, 6, C_YELLOW);
    }

    /* 标签 (10px/字符, 居中) */
    int len = 0; while (d->label[len]) len++;
    text(x + ICON_SZ / 2 - len * 5, y + ICON_SZ + 8, d->label, C_WHITE);
}

static void draw_taskbar(void) {
    int tb_y = g_h - TASKBAR_H;
    fill(0, tb_y, g_w, TASKBAR_H, C_LGRAY);
    rect(0, tb_y - 2, g_w, 2, C_WHITE);
    text(10, tb_y + 6, "XEKernelOS Desktop", C_BLACK);
    text(190, tb_y + 6, "ESC = Shell", C_BLUE);
}

static void draw_clock(void) {
    char t[9];
    _sys3(SYS_TIME, (int)t, 0, 0);
    int tb_y = g_h - TASKBAR_H;
    fill(g_w - 110, tb_y, 110, TASKBAR_H, C_LGRAY);
    text(g_w - 100, tb_y + 6, t, C_BLACK);
}

static void draw_desktop(void) {
    /* 背景: 深蓝 → 浅蓝 双色横带 */
    fill(0, 0, g_w, g_h / 2, C_BLUE);
    fill(0, g_h / 2, g_w, g_h / 2 - TASKBAR_H, C_BLUE);
    fill(0, 0, g_w, 24, C_LBLUE);

    text(16, 4, "XEKernelOS v0.4.0 GUI", C_WHITE);
    for (int i = 0; i < N_ICONS; i++)
        draw_icon(i);

    text(16, g_h / 2 + 40, "Click Terminal for command line, others to launch.", C_LGRAY);
    text(16, g_h / 2 + 60, "Press ESC to return to shell.", C_LGRAY);

    draw_taskbar();
    draw_clock();
}

/* ---- 程序启动 (fork + exec_fd, 同 ushell cmd_run) ---- */
static int launch(const char *file) {
    int fd = _sys3(SYS_OPEN, (int)file, 0, 0);
    if (fd < 0) return -1;

    int pid = _sys1(SYS_FORK, 0);
    if (pid == 0) {
        _sys1(SYS_EXEC_FD, fd);      /* 成功不返回 */
        _sys1(SYS_CLOSE, fd);
        _sys1(SYS_EXIT, 1);
        for (;;) {}
    }
    if (pid > 0)
        _sys1(SYS_WAITPID, pid);
    _sys1(SYS_CLOSE, fd);
    return 0;
}

/* ---- 输入事件 ---- */
struct input_event { u32 type; i32 x, y; u32 arg; };
#define EV_MOVE 1
#define EV_BTN  2
#define EV_KEY  3

extern "C" void _start(void) {
    /* 分辨率 */
    u32 info[5];
    _sys3(SYS_GETFB, (int)info, 0, 0);
    g_w = (int)info[1]; g_h = (int)info[2];
    if (g_w < 320 || g_h < 200) { g_w = 1024; g_h = 768; }

    int in_fd = _sys3(SYS_OPEN, (int)"/dev/input", 0, 0);
    if (in_fd < 0) {
        /* 无输入设备 — 直接退出回 shell */
        _sys1(SYS_EXIT, 1);
    }

    draw_desktop();

    char last_sec = -1;
    input_event ev;
    for (;;) {
        int n = _sys3(SYS_FREAD, in_fd, (int)&ev, (int)sizeof(ev));
        if (n == (int)sizeof(ev)) {
            if (ev.type == EV_KEY && (ev.arg == 1 || ev.arg == 27)) {
                /* ESC 扫描码 1 / ascii 27 */
                _sys1(SYS_CLOSE, in_fd);
                _sys1(SYS_EXIT, 0);
            }
            if (ev.type == EV_BTN && (ev.arg & 1)) {   /* 左键按下 */
                for (int i = 0; i < N_ICONS; i++) {
                    int x = icon_x(i), y = icon_y();
                    if (ev.x >= x - 2 && ev.x < x + ICON_SZ + 2 &&
                        ev.y >= y - 2 && ev.y < y + ICON_SZ + 2) {
                        launch(ICONS[i].file);
                        draw_desktop();   /* 程序退出后恢复桌面 */
                        break;
                    }
                }
            }
        } else {
            /* 无事件: 小睡 12ms (阻塞调度, PIT 到期唤醒) + 每秒刷新时钟 */
            _sys1(SYS_SLEEP, 12);
            char t[9];
            _sys3(SYS_TIME, (int)t, 0, 0);
            if (t[7] != last_sec) {
                last_sec = t[7];
                draw_clock();
            }
        }
    }
}
