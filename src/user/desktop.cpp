/* XEKernelOS 桌面环境 v2 (窗口管理器 + 中文界面)
   - 窗口管理器: 拖动 / 关闭(X) / 点击置顶, 中文标题栏
   - 资源管理器窗口: 列目录 / 进入子目录 / 返回上级 / 查看文本文件
   - 终端窗口: 命令输入 + 历史输出 (HELP/CLS/TIME/ECHO/LS/CD/VER/RUN)
   - 桌面图标: 文件管理器 / 终端 / 全屏演示程序
   - 输入: /dev/input 统一事件流; 绘制: framebuffer ioctl
   - 退出: ESC 回到用户 Shell */

typedef unsigned int  u32;
typedef int           i32;
typedef unsigned short u16;
typedef unsigned char  u8;

/* ---- syscall numbers ---- */
#define SYS_GETFB     9
#define SYS_TIME      8
#define SYS_SLEEP    12
#define SYS_FORK     24
#define SYS_WAITPID  26
#define SYS_IOCTL    46
#define SYS_EXEC_FD  47
#define SYS_OPEN      4
#define SYS_FREAD     5
#define SYS_CLOSE    10
#define SYS_GETCWD    7
#define SYS_FAT_CD   18
#define SYS_VFS_LIST 50
#define SYS_EXIT      2

/* ioctl commands */
#define IOCTL_FILL    3
#define IOCTL_RECT    5
#define IOCTL_TEXT    7
#define IOCTL_TEXT_UTF8 9

#define FB_FD 0

/* 调色板 */
#define C_BLACK  0x00
#define C_BLUE   0x01
#define C_LBLUE  0x09
#define C_LGRAY  0x07
#define C_WHITE  0x0F
#define C_RED    0x04
#define C_LRED   0x0C
#define C_YELLOW 0x0E
#define C_LGREEN 0x0A
#define C_LCYAN  0x0B
#define C_DGRAY  0x08
#define C_GREEN  0x02

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
    struct { short x, y, w, h; unsigned char c; } __attribute__((packed)) p;
    p.x=(short)x; p.y=(short)y; p.w=(short)w; p.h=(short)h; p.c=c;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_FILL, (int)&p);
}
static void rect(int x, int y, int w, int h, u32 c) {
    struct { short x, y, w, h; unsigned char c; } __attribute__((packed)) p;
    p.x=(short)x; p.y=(short)y; p.w=(short)w; p.h=(short)h; p.c=c;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_RECT, (int)&p);
}
static void text(int x, int y, const char *s, u32 c) {
    struct { short x, y; unsigned char c; char t[60]; } __attribute__((packed)) p;
    p.x=(short)x; p.y=(short)y; p.c=c;
    int i=0; while (s[i] && i<59) { p.t[i]=s[i]; i++; } p.t[i]=0;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_TEXT, (int)&p);
}
static void text_cn(int x, int y, const char *s, u32 c) {
    struct { short x, y; unsigned char c; char t[80]; } __attribute__((packed)) p;
    p.x=(short)x; p.y=(short)y; p.c=c;
    int i=0; while (s[i] && i<79) { p.t[i]=s[i]; i++; } p.t[i]=0;
    _sys3(SYS_IOCTL, FB_FD, IOCTL_TEXT_UTF8, (int)&p);
}

/* ---- 目录项 (与内核 DirEntry 一致) ---- */
struct dent { int is_dir; char name[32]; };

/* ---- 全屏运行程序 (fork + exec_fd) ---- */
static int launch(const char *file) {
    int fd = _sys3(SYS_OPEN, (int)file, 0, 0);
    if (fd < 0) return -1;
    int pid = _sys1(SYS_FORK, 0);
    if (pid == 0) {
        _sys1(SYS_EXEC_FD, fd);
        _sys1(SYS_CLOSE, fd);
        _sys1(SYS_EXIT, 1);
        for (;;) {}
    }
    if (pid > 0) _sys1(SYS_WAITPID, pid);
    _sys1(SYS_CLOSE, fd);
    return 0;
}

/* ---- 窗口管理器 ---- */
#define TB 18                /* 标题栏高 */
#define MAXW 2
#define T_HIST 16            /* 终端历史行数 */
#define T_LINE 64
#define EXPL_VIEW_LIST 0
#define EXPL_VIEW_FILE 1

struct wnd {
    int used, type, active;
    int x, y, w, h;
    int drag, dx, dy;
    /* 资源管理器 */
    int escroll;
    int eview;
    char efile[32];
    char ebuf[2048];
    int elen, escroll2;
    /* 终端 */
    int trow;
    char thist[T_HIST][T_LINE];
    char cmd[T_LINE];
    int cmdlen;
};
/* 窗口类型 */
#define WT_EXPL 0
#define WT_TERM 1

static struct wnd wins[MAXW];
static int active_win = -1;   /* 当前激活窗口索引 */

static void term_print(struct wnd *w, const char *s) {
    int i = 0;
    while (s[i] && i < T_LINE-1) { w->thist[w->trow][i] = s[i]; i++; }
    w->thist[w->trow][i] = 0;
    w->trow = (w->trow + 1) % T_HIST;
}

static void term_clear(struct wnd *w) {
    for (int i = 0; i < T_HIST; i++) w->thist[i][0] = 0;
    w->trow = 0;
}

static void term_exec(struct wnd *w, const char *cmd) {
    char echo[T_LINE+2];
    { int i=0; echo[i++]='>'; echo[i]=' '; int j=0; while(cmd[j]&&i<T_LINE-1) echo[i++]=cmd[j++]; echo[i]=0; }
    term_print(w, echo);
    if (!cmd[0]) return;

    if (cmd[0]=='H'&&cmd[1]=='E'&&cmd[2]=='L'&&cmd[3]=='P'&&cmd[4]==0) {
        term_print(w, "命令: HELP CLS TIME ECHO LS CD VER RUN");
    } else if (cmd[0]=='C'&&cmd[1]=='L'&&cmd[2]=='S'&&cmd[3]==0) {
        term_clear(w);
    } else if (cmd[0]=='V'&&cmd[1]=='E'&&cmd[2]=='R'&&cmd[3]==0) {
        term_print(w, "XEKernelOS v0.4.0 GUI");
    } else if (cmd[0]=='T'&&cmd[1]=='I'&&cmd[2]=='M'&&cmd[3]=='E'&&cmd[4]==0) {
        char t[9]; _sys3(SYS_TIME, (int)t, 0, 0);
        term_print(w, t);
    } else if (cmd[0]=='E'&&cmd[1]=='C'&&cmd[2]=='H'&&cmd[3]=='O') {
        const char *a = cmd+4; while(*a==' ')a++; term_print(w, a);
    } else if (cmd[0]=='C'&&cmd[1]=='D') {
        const char *a = cmd+2; while(*a==' ')a++;
        if (!*a) { _sys1(SYS_FAT_CD, (int)"\\"); term_print(w, "回到根目录"); }
        else { int r = _sys1(SYS_FAT_CD, (int)a); term_print(w, r? "目录不存在":"已进入"); }
        char cwd[64]; _sys3(SYS_GETCWD, (int)cwd, 0, 0); term_print(w, cwd);
    } else if (cmd[0]=='L'&&cmd[1]=='S'&&cmd[2]==0) {
        dent d[24]; int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 24);
        if (n < 0) { term_print(w, "读取失败"); }
        else if (n == 0) { term_print(w, "(空目录)"); }
        else for (int i = 0; i < n; i++) term_print(w, d[i].name);
    } else if (cmd[0]=='R'&&cmd[1]=='U'&&cmd[2]=='N') {
        const char *a = cmd+3; while(*a==' ')a++;
        if (!*a) term_print(w, "用法: RUN <文件.BIN>");
        else { term_print(w, "运行中..."); launch(a); }
    } else {
        term_print(w, "未知命令 (HELP 查看)");
    }
}

/* ---- 桌面布局 ---- */
static int g_w = 1024, g_h = 768;
#define TASKBAR_H 28
#define ICON_SZ 48

struct icon { const char *label; int act; const char *file; };
enum { I_EXPL, I_TERM, I_GFX, I_BOUNCE };
static const struct icon ICONS[] = {
    { "文件", I_EXPL,  0 },
    { "终端", I_TERM,  0 },
    { "演示", I_GFX,  "GFXDEMO.BIN" },
    { "弹球", I_BOUNCE,"BOUNCE.BIN" },
};
#define N_ICONS 4
static int icon_x(int i) { return 60 + i * 110; }
static int icon_y(void)  { return 80; }

static void draw_icon(int i) {
    int x = icon_x(i), y = icon_y();
    const struct icon *d = &ICONS[i];
    fill(x, y, ICON_SZ, ICON_SZ, C_BLUE);
    rect(x-2, y-2, ICON_SZ+4, ICON_SZ+4, C_WHITE);
    if (d->act == I_GFX) {
        fill(x+8,y+8,12,32,C_LRED); fill(x+20,y+8,8,32,C_LGREEN); fill(x+28,y+8,12,32,C_YELLOW);
    } else if (d->act == I_BOUNCE) {
        fill(x+14,y+10,20,28,C_YELLOW); fill(x+10,y+16,28,16,C_YELLOW); fill(x+18,y+8,12,32,C_YELLOW);
    } else if (d->act == I_EXPL) {        /* 文件夹 */
        fill(x+10,y+14,28,24,C_YELLOW); fill(x+10,y+10,14,6,C_LGRAY);
    } else {                              /* 终端: 黑窗 + >_ */
        fill(x+8,y+8,32,32,C_BLACK); rect(x+8,y+8,32,32,C_WHITE);
        fill(x+14,y+14,5,5,C_WHITE); fill(x+19,y+19,5,5,C_WHITE); fill(x+14,y+24,5,5,C_WHITE);
        fill(x+26,y+30,10,4,C_WHITE);
    }
    int len=0; while (d->label[len]) len++;
    text_cn(x + ICON_SZ/2 - len*4, y + ICON_SZ + 6, d->label, C_WHITE);
}

static void draw_taskbar(const char *clock) {
    int tb_y = g_h - TASKBAR_H;
    fill(0, tb_y, g_w, TASKBAR_H, C_LGRAY);
    rect(0, tb_y-2, g_w, 2, C_WHITE);
    text_cn(10, tb_y+6, "XEKernelOS 桌面", C_BLACK);
    text_cn(190, tb_y+6, "ESC 返回命令行", C_BLUE);
    fill(g_w-110, tb_y, 110, TASKBAR_H, C_LGRAY);
    if (clock) text(g_w-104, tb_y+6, clock, C_BLACK);
}

static char clock_buf[9];

static void draw_desktop_base(void) {
    fill(0, 0, g_w, g_h/2, C_BLUE);
    fill(0, g_h/2, g_w, g_h/2 - TASKBAR_H, C_BLUE);
    fill(0, 0, g_w, 24, C_LBLUE);
    text_cn(16, 4, "XEKernelOS 图形界面", C_WHITE);
    for (int i = 0; i < N_ICONS; i++) draw_icon(i);
    text_cn(16, g_h/2 + 40, "点击图标打开窗口或运行程序", C_LGRAY);
    text_cn(16, g_h/2 + 60, "窗口可拖动, 点 X 关闭, 点标题栏置顶", C_LGRAY);
    draw_taskbar(clock_buf);
}

/* ---- 窗口绘制 ---- */
static void draw_window_title(struct wnd *w, const char *title) {
    fill(w->x, w->y, w->w, TB, w->active ? C_BLUE : C_DGRAY);
    rect(w->x, w->y, w->w, 1, C_WHITE);
    text_cn(w->x+6, w->y+2, title, C_WHITE);
    /* X 关闭按钮 */
    fill(w->x+w->w-16, w->y+3, 12, 12, C_LRED);
    rect(w->x+w->w-16, w->y+3, 12, 12, C_WHITE);
    text(w->x+w->w-13, w->y+1, "X", C_WHITE);
}

static void draw_win_expl(struct wnd *w) {
    draw_window_title(w, "文件管理器");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_WHITE);
    /* 路径栏 */
    char cwd[64];
    _sys3(SYS_GETCWD, (int)cwd, 0, 0);
    fill(cx+2, cy+2, cw-4, 16, C_LGRAY);
    { char s[72]; s[0]=0; int i=0; while(cwd[i]&&i<71){s[i]=cwd[i];i++;} s[i]=0;
      text_cn(cx+6, cy+3, s, C_BLACK); }
    if (w->eview == EXPL_VIEW_FILE) {
        /* 文件查看 */
        int y = cy + 22;
        text_cn(cx+6, y, "返回", C_BLUE); y += 18;
        if (w->elen > 0)
            text_cn(cx+6, y, w->ebuf, C_BLACK);
        return;
    }
    /* 列表 */
    dent d[32];
    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
    int y = cy + 22;
    /* .. 返回上级 */
    text_cn(cx+6, y, "[..] 返回上级", C_BLUE); y += 16;
    if (n >= 0) {
        for (int i = 0; i < n; i++) {
            if (y > cy + ch - 6) break;
            if (d[i].is_dir) {
                char s[40]; s[0]='['; s[1]=']'; s[2]=' ';
                int j=0; while(d[i].name[j]&&j<35){s[3+j]=d[i].name[j];j++;} s[3+j]=0;
                text_cn(cx+6, y, s, C_BLUE);
            } else {
                text_cn(cx+6, y, d[i].name, C_BLACK);
            }
            y += 16;
        }
    } else {
        text_cn(cx+6, y, "读取目录失败", C_RED);
    }
}

static void draw_win_term(struct wnd *w) {
    draw_window_title(w, "终端");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_BLACK);
    int rows = ch / 16;
    /* 从最旧开始显示历史 (环形) */
    int vis[T_HIST];
    int vn = 0;
    for (int i = 0; i < T_HIST; i++) {
        int idx = (w->trow + i) % T_HIST;
        if (w->thist[idx][0]) { vis[vn++] = idx; }
    }
    if (vn > rows - 1) vn = rows - 1;
    int y = cy;
    for (int i = 0; i < vn; i++) {
        text_cn(cx+4, y, w->thist[vis[i]], C_LGREEN);
        y += 16;
    }
    /* 输入行 */
    if (y < cy + ch - 16) {
        char sb[T_LINE+2]; sb[0]='>'; sb[1]=' ';
        for (int i = 0; i < w->cmdlen; i++) sb[2+i]=w->cmd[i];
        sb[2+w->cmdlen]=0;
        text_cn(cx+4, y, sb, C_WHITE);
    }
}

static void draw_window(struct wnd *w) {
    if (w->type == WT_EXPL) draw_win_expl(w);
    else draw_win_term(w);
    /* 边框 */
    rect(w->x-1, w->y-1, w->w+2, w->h+2, C_WHITE);
}

static void redraw(void) {
    draw_desktop_base();
    for (int i = 0; i < MAXW; i++)
        if (wins[i].used) draw_window(&wins[i]);
}

/* ---- 窗口 hit-test / 交互 ---- */
static int win_hit_title(struct wnd *w, int x, int y) {
    return x >= w->x && x < w->x+w->w && y >= w->y && y < w->y+TB;
}
static int win_hit_xbtn(struct wnd *w, int x, int y) {
    return x >= w->x+w->w-18 && x < w->x+w->w && y >= w->y && y < w->y+TB;
}
static int win_contains(struct wnd *w, int x, int y) {
    return x >= w->x && x < w->x+w->w && y >= w->y && y < w->y+w->h;
}

/* 把窗口置顶: 更新 active 标记 */
static void win_raise(struct wnd *w) {
    for (int i = 0; i < MAXW; i++)
        wins[i].active = (&wins[i] == w);
    active_win = -1;
    for (int i = 0; i < MAXW; i++)
        if (&wins[i] == w) { active_win = i; break; }
}

static struct wnd *open_window(int type, int x, int y, int w, int h) {
    for (int i = 0; i < MAXW; i++) {
        if (wins[i].used) continue;
        wins[i].used = 1; wins[i].type = type;
        wins[i].x = x; wins[i].y = y; wins[i].w = w; wins[i].h = h;
        wins[i].drag = 0;
        wins[i].escroll = 0; wins[i].eview = EXPL_VIEW_LIST;
        wins[i].efile[0] = 0; wins[i].elen = 0; wins[i].escroll2 = 0;
        wins[i].trow = 0; wins[i].cmdlen = 0; wins[i].cmd[0] = 0;
        for (int j = 0; j < T_HIST; j++) wins[i].thist[j][0] = 0;
        if (type == WT_TERM) {
            term_print(&wins[i], "XEKernelOS 终端");
            term_print(&wins[i], "输入 HELP 查看命令");
        }
        for (int k = 0; k < MAXW; k++) wins[k].active = (&wins[k] == &wins[i]);
        active_win = i;
        return &wins[i];
    }
    return 0;
}

static void close_window(int idx) {
    wins[idx].used = 0;
    if (active_win == idx) active_win = -1;
    redraw();
}

/* ---- 输入事件 ---- */
struct input_event { u32 type; i32 x, y; u32 arg; };
#define EV_MOVE 1
#define EV_BTN  2
#define EV_KEY  3

/* Set1 扫描码 → ASCII (无 shift) */
static const char SC_LO[0x60] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,
    'q','w','e','r','t','y','u','i','o','p','[',']',13,0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const char SC_SH[0x60] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+',8,9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,
};

static int sc_shift = 0;

static void handle_key(u8 sc, struct wnd *termw) {
    if (sc == 0x1C) {           /* Enter */
        if (termw) { term_exec(termw, termw->cmd); termw->cmdlen=0; termw->cmd[0]=0; redraw(); }
        return;
    }
    if (sc == 0x0E) {           /* Backspace */
        if (termw && termw->cmdlen > 0) { termw->cmdlen--; termw->cmd[termw->cmdlen]=0; redraw(); }
        return;
    }
    if (sc == 0x2A || sc == 0x36) { sc_shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { sc_shift = 0; return; }
    if (sc >= 0x60) return;
    char c;
    if (sc_shift) c = SC_SH[sc];
    else c = SC_LO[sc];
    if (!c || c < 32) return;
    if (termw && termw->cmdlen < T_LINE-2) {
        termw->cmd[termw->cmdlen++] = c;
        termw->cmd[termw->cmdlen] = 0;
        redraw();
    }
}

extern "C" void _start(void) {
    u32 info[5];
    _sys3(SYS_GETFB, (int)info, 0, 0);
    g_w = (int)info[1]; g_h = (int)info[2];
    if (g_w < 320 || g_h < 200) { g_w = 1024; g_h = 768; }

    int in_fd = _sys3(SYS_OPEN, (int)"/dev/input", 0, 0);
    if (in_fd < 0) { _sys1(SYS_EXIT, 1); }

    /* 初始时钟 */
    _sys3(SYS_TIME, (int)clock_buf, 0, 0);
    redraw();

    char last_sec = -1;
    input_event ev;
    int down = 0;
    for (;;) {
        int n = _sys3(SYS_FREAD, in_fd, (int)&ev, (int)sizeof(ev));
        if (n == (int)sizeof(ev)) {
            if (ev.type == EV_KEY) {
                if (ev.arg == 1 || ev.arg == 27) {
                    _sys1(SYS_CLOSE, in_fd);
                    _sys1(SYS_EXIT, 0);
                }
                /* 键盘送终端窗口 (若有) */
                struct wnd *t = 0;
                for (int i = 0; i < MAXW; i++)
                    if (wins[i].used && wins[i].type == WT_TERM) { t = &wins[i]; break; }
                handle_key((u8)ev.arg, t);
            }
            if (ev.type == EV_BTN) {
                if (ev.arg & 1) {
                    down = 1;
                    /* 命中窗口 (从顶层往下) */
                    int handled = 0;
                    for (int i = MAXW-1; i >= 0; i--) {
                        if (!wins[i].used) continue;
                        struct wnd *w = &wins[i];
                        if (win_hit_xbtn(w, ev.x, ev.y)) {
                            close_window(i); handled = 1; break;
                        }
                        if (win_hit_title(w, ev.x, ev.y)) {
                            w->drag = 1; w->dx = ev.x - w->x; w->dy = ev.y - w->y;
                            win_raise(w); redraw(); handled = 1; break;
                        }
                        if (win_contains(w, ev.x, ev.y)) {
                            win_raise(w);
                            /* 窗口内容点击 */
                            if (w->type == WT_EXPL) {
                                int cyy = ev.y - (w->y + TB);
                                if (cyy >= 22 && cyy < 22+16) {
                                    /* 第一行: [..] 返回 */
                                    _sys1(SYS_FAT_CD, (int)"..");
                                    w->eview = EXPL_VIEW_LIST;
                                    redraw();
                                } else if (cyy >= 38) {
                                    int idx = (cyy - 38) / 16;
                                    dent d[32];
                                    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
                                    if (n > 0 && idx < n) {
                                        if (d[idx].is_dir) {
                                            _sys1(SYS_FAT_CD, (int)d[idx].name);
                                            w->eview = EXPL_VIEW_LIST;
                                        } else {
                                            int fd = _sys3(SYS_OPEN, (int)d[idx].name, 0, 0);
                                            if (fd >= 0) {
                                                int r = _sys3(SYS_FREAD, fd, (int)w->ebuf, 2000);
                                                _sys1(SYS_CLOSE, fd);
                                                if (r >= 0) { w->elen = r; w->ebuf[r] = 0; }
                                                else w->elen = 0;
                                                w->eview = EXPL_VIEW_FILE;
                                            }
                                        }
                                        redraw();
                                    }
                                }
                            }
                            handled = 1; break;
                        }
                    }
                    if (!handled) {
                        /* 桌面图标 */
                        for (int i = 0; i < N_ICONS; i++) {
                            int x = icon_x(i), y = icon_y();
                            if (ev.x >= x-2 && ev.x < x+ICON_SZ+2 &&
                                ev.y >= y-2 && ev.y < y+ICON_SZ+2) {
                                if (ICONS[i].act == I_EXPL) {
                                    open_window(WT_EXPL, 60, 150, 360, 300);
                                } else if (ICONS[i].act == I_TERM) {
                                    open_window(WT_TERM, 460, 150, 420, 260);
                                } else {
                                    launch(ICONS[i].file);
                                }
                                redraw();
                                break;
                            }
                        }
                    }
                } else {
                    down = 0;
                    for (int i = 0; i < MAXW; i++)
                        if (wins[i].used && wins[i].drag) wins[i].drag = 0;
                    redraw();
                }
            }
            if (ev.type == EV_MOVE && down) {
                for (int i = 0; i < MAXW; i++) {
                    if (wins[i].used && wins[i].drag) {
                        wins[i].x = ev.x - wins[i].dx;
                        wins[i].y = ev.y - wins[i].dy;
                        if (wins[i].y < 24) wins[i].y = 24;
                        if (wins[i].y + wins[i].h > g_h - TASKBAR_H) wins[i].y = g_h - TASKBAR_H - wins[i].h;
                        redraw();
                        break;
                    }
                }
            }
        } else {
            _sys1(SYS_SLEEP, 12);
            char t[9];
            _sys3(SYS_TIME, (int)t, 0, 0);
            if (t[7] != last_sec) {
                last_sec = t[7];
                for (int i = 0; i < 8; i++) clock_buf[i] = t[i];
                clock_buf[8] = 0;
                draw_taskbar(clock_buf);
            }
        }
    }
}