/* XEKernelOS 桌面环境 v5 (窗口管理器 + 中文界面 + 编辑器 + 右键菜单 + 系统信息)
   - 窗口: 拖动/关闭/最小化/最大化/点击置顶/边框缩放
   - 任务栏: 显示打开窗口按钮
   - 资源管理器: 显示类型/大小; 双击 .txt 编辑, .bin/.elf 运行
   - 文本编辑器: 菜单栏(文件/编辑/帮助) + 编辑 + 保存
   - 系统信息窗口: 版本/分辨率/内存
   - 右键菜单: 桌面/文件操作
   - 拖动: 重绘所有窗口消除拖影
   - 退出: ESC */

typedef unsigned int  u32;
typedef int           i32;
typedef unsigned short u16;
typedef unsigned char  u8;

#define SYS_GETFB     9
#define SYS_TIME      8
#define SYS_MOUSE    11
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
#define SYS_FAT_MKDIR 19
#define SYS_FAT_RMDIR 20
#define SYS_FAT_DEL  21
#define SYS_FAT_RENAME 22
#define SYS_FAT_WRITE 23
#define SYS_MEMINFO  49
#define SYS_VFS_LIST 50
#define SYS_TASK_LIST 51
#define SYS_KILL     28
#define SIGKILL       9
#define SYS_EXIT      2

#define IOCTL_FILL    3
#define IOCTL_RECT    5
#define IOCTL_TEXT    7
#define IOCTL_TEXT_UTF8 9
#define FB_FD 0

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
static inline int _sys2(int n, int a, int b) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b) : "memory");
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

struct dent { int is_dir; int size; char name[32]; };

/* 输入光标闪烁状态 (主循环空闲分支翻转, ~0.5s) */
static int blink_on = 1;
/* UTF-8 字符数 (每个字符 8px 宽, 用于居中计算) */
static int utf8_chars(const char *s) {
    int n = 0, i = 0;
    while (s[i]) {
        u8 c = (u8)s[i];
        if ((c & 0x80) == 0) i += 1;        /* ASCII */
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3; /* CJK */
        else i += 4;
        n++;
    }
    return n;
}
/* UTF-8 渲染宽度: ASCII 8px, CJK 16px */
static int utf8_width(const char *s, int len) {
    int w = 0, i = 0;
    while (s[i] && i < len) {
        u8 c = (u8)s[i];
        if ((c & 0x80) == 0) { w += 8; i += 1; }
        else if ((c & 0xE0) == 0xC0) { w += 16; i += 2; }
        else if ((c & 0xF0) == 0xE0) { w += 16; i += 3; }
        else { w += 16; i += 4; }
    }
    return w;
}
/* UTF-8 整字符退格: 从末尾删掉一个完整字符 (中文一次删掉,
   修复旧版按字节删导致残缺 UTF-8 显示成方块) */
static void bs_utf8(char *buf, int *len) {
    if (*len <= 0) return;
    int n = *len - 1;
    while (n > 0 && ((u8)buf[n] & 0xC0) == 0x80) n--;  /* 跳过续字节 */
    for (int i = n; i < *len; i++) buf[i] = 0;
    *len = n;
}

/* freestanding: 编译器可能对 struct dent 生成 memcpy */
extern "C" void *memcpy(void *dst, const void *src, unsigned int n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}

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
static int ends_with(const char *s, const char *suffix) {
    int ls = 0, lx = 0;
    while (s[ls]) ls++;
    while (suffix[lx]) lx++;
    if (ls < lx) return 0;
    for (int i = 0; i < lx; i++) {
        char a = s[ls-lx+i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}
static int is_exec_file(const char *n) { return ends_with(n, ".BIN") || ends_with(n, ".ELF"); }
static int is_text_file(const char *n) { return ends_with(n, ".TXT") || ends_with(n, ".MD"); }

#define TB 18
#define MAXW 8
#define SBW 12
#define T_HIST 16
#define T_LINE 64
#define EXPL_VIEW_LIST 0
#define EXPL_VIEW_FILE 1
#define WT_EXPL 0
#define WT_TERM 1
#define WT_EDIT 2
#define WT_SYSINFO 3
#define WT_CTRL 4
#define WT_DLG  5
#define WT_CALC 6
#define WT_TASK 7
#define EDIT_MAX 2048

/* ---- 通用控件系统 ---- */
#define CT_BUTTON   0
#define CT_PROGRESS 1
#define CT_SLIDER   2
#define CT_EDIT     3
#define CT_CHECK    4
#define CT_RADIO    5
#define CT_MENUBAR  6
#define CL_MAX 32
struct ctrl {
    int type;
    int x, y, w, h;
    int val;         /* check/radio: 0/1; slider/progress: 0..100 */
    int grp;         /* radio 分组 */
    int id;          /* 动作 id */
    int pressed;     /* 按下/悬停态 */
    char label[24];
};
/* 控件动作 id */
#define CA_MFILE   60   /* 菜单栏: 文件 */
#define CA_MEDIT   61   /* 菜单栏: 编辑 */
#define CA_MHELP   62   /* 菜单栏: 帮助 */
#define CA_INFO    63   /* 弹信息框 */
#define CA_PROGUP  64   /* 进度+20 */
#define CA_PROGDN  65   /* 进度-20 */
#define CA_OK      66   /* 对话框确定 */
#define CA_SAVENOW 67   /* 编辑器保存 */
#define CAL_KEY    70   /* 计算器按键 */
#define CA_MKDIR   71   /* 新建文件夹 */
#define CA_DELSEL  72   /* 删除选中项 */
#define CA_RENAME  73   /* 重命名选中项 */
#define CA_RENOK   74   /* 重命名对话框确定 */
#define CA_TKILL   75   /* 任务管理器: 结束选中进程 */
#define CA_TKREF   76   /* 任务管理器: 刷新 */

/* 任务快照 — 必须与内核 struct task_info 布局一致 */
struct tinfo { u32 pid, state, priority, ring3; };
#define TASK_MAX 16

struct wnd {
    int used, type, active;
    int x, y, w, h;
    int nx, ny, nw, nh;
    int minimized, maximized;
    int drag, resize, rdir, dx, dy;
    int tbtn_x;
    int escroll, eview;
    char efile[32];
    char ebuf[2048];
    int elen, escroll2;
    int trow;
    char thist[T_HIST][T_LINE];
    char cmd[T_LINE];
    int cmdlen;
    char edit_buf[EDIT_MAX];
    int edit_len;
    char edit_path[32];
    struct ctrl ctrls[CL_MAX];
    int nctrl;
    int cfocus;       /* 编辑框焦点索引, -1 无 */
    int slider_drag;  /* 正在拖动的滑块控件索引 */
    int vscroll;      /* 垂直滚动偏移 */
    int sb_drag;      /* 正在拖动滚动条滑块 */
    int esel;         /* 资源管理器选中项索引, -1 无 */
    char dlg_msg[64];
    char calc_expr[64];
    int calc_len;
    char ren_from[32];  /* 重命名对话框: 原名 (空=普通信息框) */
};

static struct wnd wins[MAXW];
static int order[MAXW];
static int norder = 0;
static int active_win = -1;

static void order_update_active(void) {
    for (int i = 0; i < MAXW; i++) wins[i].active = 0;
    if (norder > 0) wins[order[norder-1]].active = 1;
    active_win = norder > 0 ? order[norder-1] : -1;
}
static const char *win_title(const struct wnd *w) {
    if (w->type == WT_EXPL) return "文件管理器";
    if (w->type == WT_TERM) return "终端";
    if (w->type == WT_SYSINFO) return "系统信息";
    if (w->type == WT_CALC) return "计算器";
    if (w->type == WT_TASK) return "任务管理器";
    return w->edit_path[0] ? w->edit_path : "编辑器";
}

/* ---- 终端 ---- */
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
static int str_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}
static void term_exec(struct wnd *w, const char *cmd) {
    char echo[T_LINE+2];
    { int i=0; echo[i++]='>'; echo[i]=' '; int j=0; while(cmd[j]&&i<T_LINE-1) echo[i++]=cmd[j++]; echo[i]=0; }
    term_print(w, echo);
    if (!cmd[0]) return;
    if (str_ci(cmd, "HELP")) term_print(w, "命令: HELP CLS TIME ECHO LS CD VER RUN");
    else if (str_ci(cmd, "CLS")) term_clear(w);
    else if (str_ci(cmd, "VER")) term_print(w, "XEKernelOS v0.4.0 GUI");
    else if (str_ci(cmd, "TIME")) { char t[9]; _sys3(SYS_TIME,(int)t,0,0); term_print(w, t); }
    else if (str_ci(cmd, "ECHO")) { const char *a=cmd+4; while(*a==' ')a++; term_print(w, a); }
    else if (cmd[0]=='C'&&cmd[1]=='D'&&(cmd[2]==' '||cmd[2]==0)) {
        const char *a=cmd+2; while(*a==' ')a++;
        if (!*a) { _sys1(SYS_FAT_CD,(int)"\\"); term_print(w,"回到根目录"); }
        else { int r=_sys1(SYS_FAT_CD,(int)a); term_print(w, r?"目录不存在":"已进入"); }
        char cwd[64]; _sys3(SYS_GETCWD,(int)cwd,0,0); term_print(w, cwd);
    } else if (str_ci(cmd, "LS")) {
        dent d[24]; int n=_sys3(SYS_VFS_LIST,(int)"",(int)d,24);
        if (n<0) term_print(w,"读取失败");
        else if (n==0) term_print(w,"(空目录)");
        else for (int i=0;i<n;i++) term_print(w, d[i].name);
    } else if (cmd[0]=='R'&&cmd[1]=='U'&&cmd[2]=='N'&&(cmd[3]==' '||cmd[3]==0)) {
        const char *a=cmd+3; while(*a==' ')a++;
        if (!*a) term_print(w,"用法: RUN <文件.BIN>");
        else { term_print(w,"运行中..."); launch(a); }
    } else term_print(w, "未知命令 (HELP 查看)");
}

/* ---- 桌面 ---- */
static int g_w = 1024, g_h = 768;
#define TASKBAR_H 28
#define ICON_SZ 48
struct icon { const char *label; int act; const char *file; };
enum { I_EXPL, I_TERM, I_EDIT, I_CALC, I_GFX, I_BOUNCE, I_TASK };
static const struct icon ICONS[] = {
    { "文件", I_EXPL, 0 }, { "终端", I_TERM, 0 },
    { "编辑器", I_EDIT, 0 }, { "计算器", I_CALC, 0 },
    { "演示", I_GFX,  "GFXDEMO.BIN" }, { "弹球", I_BOUNCE,"BOUNCE.BIN" },
    { "任务", I_TASK, 0 },
};
#define N_ICONS 7
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
    } else if (d->act == I_EXPL) {
        fill(x+10,y+14,28,24,C_YELLOW); fill(x+10,y+10,14,6,C_LGRAY);
    } else if (d->act == I_EDIT) {
        fill(x+8,y+8,32,32,C_WHITE); rect(x+8,y+8,32,32,C_BLACK);
        fill(x+12,y+14,24,2,C_BLUE); fill(x+12,y+20,24,2,C_BLUE);
        fill(x+12,y+26,18,2,C_BLUE);
    } else if (d->act == I_CALC) {
        fill(x+10,y+8,28,32,C_LGREEN); rect(x+10,y+8,28,32,C_BLACK);
        fill(x+13,y+11,10,8,C_WHITE); fill(x+26,y+11,9,8,C_WHITE);
        fill(x+13,y+22,8,5,C_WHITE); fill(x+24,y+22,8,5,C_WHITE);
        fill(x+13,y+30,8,5,C_WHITE); fill(x+24,y+30,8,5,C_WHITE);
    } else if (d->act == I_TASK) {
        fill(x+8,y+8,32,32,C_LGRAY); rect(x+8,y+8,32,32,C_BLACK);
        fill(x+12,y+13,10,4,C_BLUE);  fill(x+26,y+13,10,4,C_DGRAY);
        fill(x+12,y+21,10,4,C_GREEN); fill(x+26,y+21,10,4,C_DGRAY);
        fill(x+12,y+29,10,4,C_LRED);  fill(x+26,y+29,10,4,C_DGRAY);
    } else {
        fill(x+8,y+8,32,32,C_BLACK); rect(x+8,y+8,32,32,C_WHITE);
        fill(x+14,y+14,5,5,C_WHITE); fill(x+19,y+19,5,5,C_WHITE); fill(x+14,y+24,5,5,C_WHITE);
        fill(x+26,y+30,10,4,C_WHITE);
    }
    int len=0; while (d->label[len]) len++;
    text_cn(x + ICON_SZ/2 - len*4, y + ICON_SZ + 6, d->label, C_WHITE);
}
static char clock_buf[9];
static void draw_taskbar(const char *clock) {
    int tb_y = g_h - TASKBAR_H;
    fill(0, tb_y, g_w, TASKBAR_H, C_LGRAY);
    rect(0, tb_y-2, g_w, 2, C_WHITE);
    text_cn(10, tb_y+6, "XEKernelOS 桌面", C_BLACK);
    text_cn(200, tb_y+6, "ESC 返回", C_BLUE);
    int bx = 300;
    for (int i = 0; i < norder; i++) {
        struct wnd *w = &wins[order[i]];
        if (!w->used) continue;
        int bw = 96;
        w->tbtn_x = bx;
        fill(bx+2, tb_y+3, bw-4, TASKBAR_H-6, w->active ? C_BLUE : C_DGRAY);
        rect(bx+2, tb_y+3, bw-4, TASKBAR_H-6, C_WHITE);
        text_cn(bx+4, tb_y+6, win_title(w), C_WHITE);
        bx += bw;
    }
    fill(g_w-110, tb_y, 110, TASKBAR_H, C_LGRAY);
    if (clock) text(g_w-104, tb_y+6, clock, C_BLACK);
}
static void draw_desktop_base(void) {
    fill(0, 0, g_w, g_h/2, C_BLUE);
    fill(0, g_h/2, g_w, g_h/2 - TASKBAR_H, C_BLUE);
    fill(0, 0, g_w, 24, C_LBLUE);
    text_cn(16, 4, "XEKernelOS 图形界面", C_WHITE);
    for (int i = 0; i < N_ICONS; i++) draw_icon(i);
    text_cn(16, g_h/2 + 40, "点击图标打开窗口; 右键弹菜单", C_LGRAY);
    text_cn(16, g_h/2 + 60, "拖窗口边框可调整大小", C_LGRAY);
    draw_taskbar(clock_buf);
}

/* ---- 窗口绘制 ---- */
static int wbtn_x(struct wnd *w, int which) {  /* 0=min 1=max 2=x */
    return w->x + w->w - 16 - (2 - which) * 18;
}
static void draw_window_title(struct wnd *w, const char *title) {
    fill(w->x, w->y, w->w, TB, w->active ? C_BLUE : C_DGRAY);
    rect(w->x, w->y, w->w, 1, C_WHITE);
    text_cn(w->x+6, w->y+2, title, C_WHITE);
    fill(wbtn_x(w,0), w->y+3, 12, 12, C_LGRAY); rect(wbtn_x(w,0)+2, w->y+9, 8, 2, C_BLACK);
    fill(wbtn_x(w,1), w->y+3, 12, 12, C_LGRAY); rect(wbtn_x(w,1)+2, w->y+5, 8, 6, C_BLACK);
    fill(wbtn_x(w,2), w->y+3, 12, 12, C_LRED); rect(wbtn_x(w,2), w->y+3, 12, 12, C_WHITE);
    text(wbtn_x(w,2)+3, w->y+1, "X", C_WHITE);
}

static void itos_pad(char *out, int v) {
    char tmp[12]; int n=0;
    if (v==0) tmp[n++]='0';
    while (v) { tmp[n++]='0'+v%10; v/=10; }
    for (int i=0;i<n;i++) out[i]=tmp[n-1-i];
    out[n]=0;
}
/* ---- 滚动条 ---- */
static int task_rows(struct wnd *w);
static int win_total_lines(struct wnd *w) {
    if (w->type == WT_EDIT) { int l = 1; for (int i = 0; i < w->edit_len; i++) if (w->edit_buf[i] == '\n') l++; return l; }
    if (w->type == WT_EXPL) {
        if (w->eview == EXPL_VIEW_FILE) return 1;
        dent d[32]; int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
        return (n > 0 ? n : 0) + 1;
    }
    if (w->type == WT_TERM) return T_HIST;
    if (w->type == WT_TASK) {
        struct tinfo t[TASK_MAX];
        int n = _sys3(SYS_TASK_LIST, (int)t, TASK_MAX, 0);
        return n > 0 ? n : 0;
    }
    return 0;
}
static int scroll_visible(struct wnd *w) {
    if (w->type == WT_EDIT) { int v = (w->h - TB - 36) / 16; return v < 1 ? 1 : v; }
    if (w->type == WT_EXPL) { int v = (w->h - TB - 22) / 16; return v < 1 ? 1 : v; }
    if (w->type == WT_TERM) { int v = (w->h - TB) / 16 - 1; return v < 1 ? 1 : v; }
    if (w->type == WT_TASK) return task_rows(w);
    return 0;
}
static void scroll_geom(struct wnd *w, int visible, int *sbh, int *thumb, int *spos, int *max) {
    *sbh = w->h - TB;
    int total = win_total_lines(w);
    *max = total - visible;
    if (*max < 0) *max = 0;
    if (visible >= total) { *thumb = 0; *spos = 0; return; }
    *thumb = *sbh * visible / total;
    if (*thumb < 8) *thumb = 8;
    *spos = (*max > 0) ? (int)((long)w->vscroll * (*sbh - *thumb) / (*max)) : 0;
}
static void draw_scrollbar(struct wnd *w, int visible) {
    int sbh, thumb, spos, max;
    scroll_geom(w, visible, &sbh, &thumb, &spos, &max);
    int sbx = w->x + w->w - SBW - 8, sby = w->y + TB;
    fill(sbx, sby, SBW, sbh, C_LGRAY);
    rect(sbx, sby, SBW - 1, sbh, C_DGRAY);
    if (thumb > 0) {
        fill(sbx + 1, sby + spos, SBW - 2, thumb, C_DGRAY);
        rect(sbx + 1, sby + spos, SBW - 2, thumb, C_WHITE);
    }
}
static void scroll_set(struct wnd *w, int my, int visible) {
    int sbh, thumb, spos, max;
    scroll_geom(w, visible, &sbh, &thumb, &spos, &max);
    int rel = my - (w->y + TB) - thumb / 2;
    if (max > 0) w->vscroll = (int)((long)rel * max / (sbh - thumb));
    if (w->vscroll < 0) w->vscroll = 0;
    if (w->vscroll > max) w->vscroll = max;
}

static void ctrl_draw_button(struct wnd *w, struct ctrl *c);
static void draw_expl_row(int x, int y, const dent *d, int sel) {
    u32 cn = sel ? C_WHITE : C_BLACK, ct = sel ? C_WHITE : C_DGRAY;
    if (sel) fill(x, y, 210, 16, C_BLUE);
    if (d->is_dir) {
        char s[40]; s[0]='['; s[1]=']'; s[2]=' ';
        int j=0; while(d->name[j]&&j<35){s[3+j]=d->name[j];j++;} s[3+j]=0;
        text_cn(x+2, y, s, sel ? C_WHITE : C_BLUE);
        text_cn(x+150, y, "目录", ct);
        text_cn(x+210, y, "-", ct);
    } else {
        text_cn(x+2, y, d->name, cn);
        const char *type = "文件";
        if (is_exec_file(d->name)) type = "程序";
        else if (is_text_file(d->name)) type = "文本";
        text_cn(x+150, y, type, ct);
        char sz[12]; itos_pad(sz, d->size);
        text(x+210, y, sz, ct);
    }
}

static void draw_win_expl(struct wnd *w) {
    draw_window_title(w, "文件管理器");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_WHITE);
    char cwd[64];
    _sys3(SYS_GETCWD, (int)cwd, 0, 0);
    fill(cx+2, cy+2, cw-150 > 60 ? cw-150 : 60, 16, C_LGRAY);
    text_cn(cx+6, cy+3, cwd, C_BLACK);
    /* 工具栏按钮 (右对齐, 每次绘制重算 x 适配窗口宽度) */
    if (w->nctrl >= 3) {
        w->ctrls[w->nctrl-3].x = cw - 220;
        w->ctrls[w->nctrl-2].x = cw - 146;
        w->ctrls[w->nctrl-1].x = cw - 74;
        ctrl_draw_button(w, &w->ctrls[w->nctrl-3]);
        ctrl_draw_button(w, &w->ctrls[w->nctrl-2]);
        ctrl_draw_button(w, &w->ctrls[w->nctrl-1]);
    }
    if (w->eview == EXPL_VIEW_FILE) {
        int y = cy + 22;
        text_cn(cx+6, y, "返回", C_BLUE); y += 18;
        if (w->elen > 0) text_cn(cx+6, y, w->ebuf, C_BLACK);
        return;
    }
    dent d[32];
    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
    int y = cy + 22;
    text_cn(cx+6, y, "[..] 返回上级", C_BLUE); y += 16;
    if (n >= 0) {
        for (int i = w->vscroll; i < n; i++) {
            if (y > cy + ch - 6) break;
            draw_expl_row(cx, y, &d[i], i == w->esel);
            y += 16;
        }
    } else {
        text_cn(cx+6, y, "读取目录失败", C_RED);
    }
    draw_scrollbar(w, scroll_visible(w));
}

static void draw_win_term(struct wnd *w) {
    draw_window_title(w, "终端");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_BLACK);
    int rows = ch / 16; if (rows < 2) rows = 2;
    int maxv = T_HIST - (rows - 1); if (maxv < 0) maxv = 0;
    if (w->vscroll > maxv) w->vscroll = maxv;
    int start = w->trow - (rows - 1) - w->vscroll;
    int y = cy;
    for (int k = 0; k < rows - 1; k++) {
        int idx = ((start + k) % T_HIST + T_HIST) % T_HIST;
        if (w->thist[idx][0]) text_cn(cx+4, y, w->thist[idx], C_LGREEN);
        y += 16;
    }
    if (y < cy + ch - 16) {
        char sb[T_LINE+2]; sb[0]='>'; sb[1]=' ';
        for (int i = 0; i < w->cmdlen; i++) sb[2+i]=w->cmd[i];
        sb[2+w->cmdlen]=0;
        text_cn(cx+4, y, sb, C_WHITE);
    }
    draw_scrollbar(w, scroll_visible(w));
}

/* 编辑器菜单栏 */
#define EM_FILE 0
#define EM_EDIT 1
#define EM_HELP 2
static void ctrl_draw_edit(struct wnd *w, struct ctrl *c);
static void ctrl_draw_button(struct wnd *w, struct ctrl *c);
static void draw_win_edit(struct wnd *w) {
    draw_window_title(w, win_title(w));
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_WHITE);
    /* 菜单栏 */
    fill(cx, cy, cw, 16, C_LGRAY);
    text_cn(cx+4, cy+1, "文件", C_BLACK);
    text_cn(cx+44, cy+1, "编辑", C_BLACK);
    text_cn(cx+84, cy+1, "帮助", C_BLACK);
    int ty = cy + 18;
    /* 文件名行: 名称 + 编辑框 + 保存按钮 */
    text_cn(cx+2, ty+1, "文件名:", C_BLACK);
    if (w->nctrl > 0) ctrl_draw_edit(w, &w->ctrls[0]);
    if (w->nctrl > 1) ctrl_draw_button(w, &w->ctrls[1]);
    /* 文本区 */
    int y = ty + 18;
    int startline = w->vscroll, i = 0, cur = 0;
    while (i < w->edit_len && cur < startline) { if (w->edit_buf[i] == '\n') cur++; i++; }
    int cur_y = -1, cur_x = 0;   /* 末尾光标位置 (输入点在文本末尾) */
    while (i < w->edit_len && y < cy + ch - 6) {
        char line[56]; int n = 0;
        while (i < w->edit_len && n < 55 && w->edit_buf[i] != '\n') line[n++] = w->edit_buf[i++];
        line[n] = 0;
        if (i < w->edit_len && w->edit_buf[i] == '\n') i++;
        text_cn(cx+4, y, line, C_BLACK);
        cur_y = y; cur_x = cx + 4 + utf8_width(line, n);
        y += 16;
    }
    if (cur_y < 0) { cur_y = y; cur_x = cx + 4; }   /* 空文本: 首行行首 */
    /* 正文输入光标 (闪烁) — 与文件名编辑框焦点互斥: 正文焦点时显示 */
    if (blink_on && w->cfocus != 0 && cur_y < cy + ch - 6 && cur_x < cx + cw - 20)
        rect(cur_x + 1, cur_y, 2, 14, C_BLACK);
    draw_scrollbar(w, scroll_visible(w));
}

static void draw_win_sysinfo(struct wnd *w) {
    draw_window_title(w, "系统信息");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_WHITE);
    int y = cy + 6;
    text_cn(cx+8, y, "XEKernelOS v0.4.0", C_BLUE); y += 20;
    text_cn(cx+8, y, "架构: x86 32位保护模式", C_BLACK); y += 18;
    text_cn(cx+8, y, "分页: 4KB + 4MB PSE 启用", C_BLACK); y += 18;
    char sz[16];
    itos_pad(sz, g_w); { char s[40]="分辨率: "; int i=7; for(int k=0;sz[k];k++)s[i++]=sz[k]; s[i++]='x'; itos_pad(sz,g_h); for(int k=0;sz[k];k++)s[i++]=sz[k]; s[i]=0; text_cn(cx+8,y,s,C_BLACK); }
    y += 18;
    int fp, tp;
    __asm__ volatile("int $0x80" : "=a"(fp), "=b"(tp) : "a"(49) : "memory");
    {
        char s[40]; int i=0; const char *h="空闲内存: ";
        while(h[i]){s[i]=h[i];i++;}
        itos_pad(sz, fp*4); for(int k=0;sz[k];k++)s[i++]=sz[k];
        s[i++]='K'; s[i++]='B'; s[i]=0;
        text_cn(cx+8, y, s, C_BLACK); y += 18;
    }
    {
        char s[40]; int i=0; const char *h="总内存: ";
        while(h[i]){s[i]=h[i];i++;}
        itos_pad(sz, tp*4); for(int k=0;sz[k];k++)s[i++]=sz[k];
        s[i++]='K'; s[i++]='B'; s[i]=0;
        text_cn(cx+8, y, s, C_BLACK); y += 18;
    }
    text_cn(cx+8, y, "内核: 引导 ELF 加载器", C_BLACK); y += 18;
    text_cn(cx+8, y, "文件系统: FAT12 + ext2", C_BLACK); y += 18;
    text_cn(cx+8, y, "显示: 帧缓冲 VESA 1024x768", C_BLACK); y += 18;
    text_cn(cx+8, y, "输入: PS/2 键盘 + 鼠标", C_BLACK); y += 18;
    text_cn(cx+8, y, "调度: 动态优先级时间片轮转", C_BLACK);
}

/* ---- 任务管理器 ---- */
static const char *task_state_name(u32 st) {
    if (st == 0) return "运行";
    if (st == 1) return "就绪";
    if (st == 2) return "阻塞";
    return "退出";
}
/* 列表可见行数 (绘制与滚动条必须用同一公式) */
static int task_rows(struct wnd *w) {
    int r = (w->h - TB - 26) / 16;
    return r < 1 ? 1 : r;
}
static void draw_win_task(struct wnd *w) {
    draw_window_title(w, "任务管理器");
    int cx = w->x, cy = w->y + TB;
    int cw = w->w, ch = w->h - TB;
    fill(cx, cy, cw, ch, C_WHITE);
    /* 工具栏按钮 (右对齐, 停靠滚动条左侧; 每次绘制按窗口宽度重算) */
    if (w->nctrl >= 2) {
        w->ctrls[0].x = cw - 165;
        w->ctrls[1].x = cw - 93;
        ctrl_draw_button(w, &w->ctrls[0]);
        ctrl_draw_button(w, &w->ctrls[1]);
    }
    text_cn(cx+6, cy+4, "PID", C_DGRAY);
    text_cn(cx+46, cy+4, "状态", C_DGRAY);
    text_cn(cx+94, cy+4, "优先级", C_DGRAY);
    text_cn(cx+156, cy+4, "类型", C_DGRAY);
    struct tinfo t[TASK_MAX];
    int n = _sys3(SYS_TASK_LIST, (int)t, TASK_MAX, 0);
    if (n < 0) n = 0;
    if (n == 0) { text_cn(cx+6, cy+30, "读取任务失败", C_RED); return; }
    int vis = task_rows(w);
    for (int i = 0; i < vis; i++) {
        int idx = w->vscroll + i;
        if (idx >= n) break;
        int ry = cy + 24 + i * 16;
        int sel = (idx == w->esel);
        if (sel) fill(cx+2, ry-1, cw-8, 16, C_BLUE);
        u32 c = sel ? C_WHITE : C_BLACK;
        char sz[12];
        itos_pad(sz, (int)t[idx].pid);
        text(cx+6, ry, sz, c);
        text_cn(cx+46, ry, task_state_name(t[idx].state), c);
        itos_pad(sz, (int)t[idx].priority);
        text(cx+94, ry, sz, c);
        text_cn(cx+156, ry, t[idx].ring3 ? "用户" : "内核", c);
    }
}

/* ---- 通用控件绘制 ---- */
static struct ctrl *wadd(struct wnd *w, int type, int x, int y, int ww, int hh, int val, int id, const char *label) {
    if (w->nctrl >= CL_MAX) return 0;
    struct ctrl *c = &w->ctrls[w->nctrl++];
    c->type = type; c->x = x; c->y = y; c->w = ww; c->h = hh;
    c->val = val; c->id = id; c->pressed = 0; c->grp = 0;
    int i = 0; while (label && label[i] && i < 23) { c->label[i] = label[i]; i++; }
    c->label[i] = 0;
    return c;
}
static void ctrl_draw_button(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    fill(x, y, c->w, c->h, c->pressed ? C_LGREEN : C_LGRAY);
    rect(x, y, c->w, c->h, C_BLACK);
    int tw = utf8_chars(c->label) * 8, tx = x + (c->w - tw) / 2;
    if (tx < x + 2) tx = x + 2;
    text_cn(tx, y + 1, c->label, C_BLACK);
}
static void ctrl_draw_progress(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    fill(x, y, c->w, c->h, C_WHITE); rect(x, y, c->w, c->h, C_BLACK);
    int fw = c->w * c->val / 100;
    if (fw < 0) fw = 0; if (fw > c->w) fw = c->w;
    if (fw > 4) fill(x + 2, y + 2, fw - 4, c->h - 4, C_GREEN);
}
static void ctrl_draw_slider(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    fill(x, y, c->w, 4, C_DGRAY);
    int kx = x + c->w * c->val / 100;
    if (kx - x < 3) kx = x + 3;
    if (kx > x + c->w - 3) kx = x + c->w - 3;
    fill(kx - 3, y - 3, 6, 10, C_LGRAY); rect(kx - 3, y - 3, 6, 10, C_BLACK);
}
static void ctrl_draw_edit(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    fill(x, y, c->w, c->h, C_WHITE); rect(x, y, c->w, c->h, C_BLACK);
    text_cn(x + 2, y + 1, c->label, C_BLACK);
    if (blink_on && w->cfocus >= 0 && &w->ctrls[w->cfocus] == c) {
        int n = 0; while (c->label[n]) n++;
        int px = x + 2 + utf8_width(c->label, n);
        if (px < x + c->w - 2) rect(px, y + 2, 2, c->h - 4, C_BLACK);  /* 闪烁光标 */
    }
}
static void ctrl_draw_check(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    rect(x + 1, y + 1, c->h - 2, c->h - 2, C_BLACK);
    if (c->val) fill(x + 3, y + 3, c->h - 6, c->h - 6, C_BLUE);
    text_cn(x + c->h + 6, y, c->label, C_BLACK);
}
static void ctrl_draw_radio(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    rect(x + 1, y + 1, c->h - 2, c->h - 2, C_BLACK);
    if (c->val) fill(x + 3, y + 3, c->h - 6, c->h - 6, C_RED);
    text_cn(x + c->h + 6, y, c->label, C_BLACK);
}
static void ctrl_draw_menubar(struct wnd *w, struct ctrl *c) {
    int x = w->x + c->x, y = w->y + c->y;
    fill(x, y, c->w, c->h, C_LGRAY);
    int dx = x + 6, i = 0;
    while (c->label[i]) {
        int j = i;
        while (c->label[j] && c->label[j] != ' ') j++;
        char seg[16]; int n = 0;
        for (int k = i; k < j && n < 15; k++) seg[n++] = c->label[k];
        seg[n] = 0;
        text_cn(dx, y + 1, seg, C_BLACK);
        dx += (j - i) / 3 * 8 + 20;
        i = (c->label[j] == ' ') ? j + 1 : j;
    }
}

static void draw_win_ctrl(struct wnd *w) {
    draw_window_title(w, "控件演示");
    int cx = w->x, cy = w->y + TB;
    fill(cx, cy, w->w, w->h - TB, C_WHITE);
    for (int i = 0; i < w->nctrl; i++) {
        struct ctrl *c = &w->ctrls[i];
        switch (c->type) {
            case CT_BUTTON:   ctrl_draw_button(w, c);   break;
            case CT_PROGRESS: ctrl_draw_progress(w, c); break;
            case CT_SLIDER:   ctrl_draw_slider(w, c);   break;
            case CT_EDIT:     ctrl_draw_edit(w, c);     break;
            case CT_CHECK:    ctrl_draw_check(w, c);    break;
            case CT_RADIO:    ctrl_draw_radio(w, c);    break;
            case CT_MENUBAR:  ctrl_draw_menubar(w, c);  break;
        }
    }
}
static void draw_win_dlg(struct wnd *w) {
    draw_window_title(w, "信息");
    int cx = w->x, cy = w->y + TB;
    fill(cx, cy, w->w, w->h - TB, C_WHITE);
    int y = cy + 14, i = 0;
    while (w->dlg_msg[i] && y < cy + w->h - TB - 20) {
        char line[64]; int n = 0;
        while (w->dlg_msg[i] && w->dlg_msg[i] != '\n' && n < 63) line[n++] = w->dlg_msg[i++];
        line[n] = 0;
        if (w->dlg_msg[i] == '\n') i++;
        text_cn(cx + 10, y, line, C_BLACK);
        y += 18;
    }
    for (int i = 0; i < w->nctrl; i++) {
        struct ctrl *c = &w->ctrls[i];
        if (c->type == CT_BUTTON) ctrl_draw_button(w, c);
        else if (c->type == CT_EDIT) ctrl_draw_edit(w, c);
    }
}

static void draw_win_calc(struct wnd *w);
static void draw_window(struct wnd *w) {
    if (w->type == WT_EXPL) draw_win_expl(w);
    else if (w->type == WT_TERM) draw_win_term(w);
    else if (w->type == WT_SYSINFO) draw_win_sysinfo(w);
    else if (w->type == WT_EDIT) draw_win_edit(w);
    else if (w->type == WT_CTRL) draw_win_ctrl(w);
    else if (w->type == WT_DLG) draw_win_dlg(w);
    else if (w->type == WT_CALC) draw_win_calc(w);
    else if (w->type == WT_TASK) draw_win_task(w);
    /* 立体边框 + 右下阴影, 多窗口叠放不融合 */
    fill(w->x + w->w, w->y + 3, 3, w->h, C_DGRAY);   /* 右阴影 */
    fill(w->x + 3, w->y + w->h, w->w, 3, C_DGRAY);   /* 下阴影 */
    rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2, C_BLACK);  /* 外层黑框 */
    rect(w->x, w->y, w->w, w->h, C_WHITE);           /* 内层白框 */
}

/* ---- 计算器 (科学计算) ---- */
static double cm_abs(double x) { return x < 0 ? -x : x; }
static double cm_sqrt(double x) {
    if (x < 0) return 0;
    double s = x / 2, last = 0;
    for (int i = 0; i < 40; i++) { last = s; if (last < 1e-30) break; s = (s + x / s) / 2; if (cm_abs(s - last) < 1e-12) break; }
    return s;
}
static double cm_exp(double x) {
    double r = 1, t = 1;
    for (int i = 1; i < 40; i++) { t *= x / i; r += t; if (cm_abs(t) < 1e-17) break; }
    return r;
}
static double cm_ln(double x) {
    if (x <= 0) return 0;
    double y = (x - 1) / (x + 1), y2 = y * y, r = 0, t = y;
    for (int i = 1; i < 60; i += 2) { r += t / i; t *= y2; if (cm_abs(t / (i + 2)) < 1e-15) break; }
    return 2 * r;
}
static double cm_log(double x) { return cm_ln(x) / cm_ln(10.0); }
static double cm_pow(double a, double b) {
    if (b == (int)b) {
        double r = 1; int n = (int)b, k = n < 0 ? -n : n;
        for (int i = 0; i < k; i++) r *= a;
        return n < 0 ? 1.0 / r : r;
    }
    if (a <= 0) return 0;
    return cm_exp(b * cm_ln(a));
}
static double cm_sin(double x) {
    double r = 0, term = x;
    for (int n = 0; n < 30; n++) { r += (n % 2 ? -term : term); term *= x * x / ((2 * n + 2) * (2 * n + 3)); }
    return r;
}
static double cm_cos(double x) {
    double r = 0, term = 1;
    for (int n = 0; n < 30; n++) { r += (n % 2 ? -term : term); term *= x * x / ((2 * n + 1) * (2 * n + 2)); }
    return r;
}
static double cm_tan(double x) {
    double c = cm_cos(x);
    if (cm_abs(c) < 1e-12) return 0;
    return cm_sin(x) / c;
}
static void fmt_double(char *out, double v) {
    if (v < 0) { *out++ = '-'; v = -v; }
    int ip = (int)v;
    double fp = v - ip;
    char tmp[24]; int n = 0;
    if (ip == 0) tmp[n++] = '0';
    while (ip) { tmp[n++] = '0' + ip % 10; ip /= 10; }
    for (int i = n - 1; i >= 0; i--) *out++ = tmp[i];
    if (fp > 1e-9) {
        *out++ = '.';
        for (int k = 0; k < 6; k++) { fp *= 10; int d = (int)fp; *out++ = '0' + d; fp -= d; if (fp < 1e-9) break; }
    }
    *out = 0;
}
/* 表达式解析器 (递归下降) */
struct P { const char *s; int i; };
static double p_primary(struct P *p);
static double p_power(struct P *p);
static double p_term(struct P *p);
static double p_expr(struct P *p);
static double p_unary(struct P *p);
static double p_power(struct P *p) {
    double base = p_unary(p);
    const char *s = p->s; int i = p->i;
    while (s[i] == ' ') i++;
    if (s[i] == '^') { i++; p->i = i; return cm_pow(base, p_power(p)); }
    p->i = i; return base;
}
static double p_term(struct P *p) {
    double v = p_power(p);
    const char *s = p->s; int i = p->i;
    for (;;) {
        while (s[i] == ' ') i++;
        if (s[i] == '*') { i++; p->i = i; v *= p_power(p); i = p->i; }
        else if (s[i] == '/') { i++; p->i = i; double d = p_power(p); if (d != 0) v /= d; else return 0; i = p->i; }
        else break;
    }
    p->i = i; return v;
}
static double p_expr(struct P *p) {
    double v = p_term(p);
    const char *s = p->s; int i = p->i;
    for (;;) {
        while (s[i] == ' ') i++;
        if (s[i] == '+') { i++; p->i = i; v += p_term(p); i = p->i; }
        else if (s[i] == '-') { i++; p->i = i; v -= p_term(p); i = p->i; }
        else break;
    }
    p->i = i; return v;
}
static double p_unary(struct P *p) {
    const char *s = p->s; int i = p->i;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { i++; p->i = i; return -p_unary(p); }
    if (s[i] == '+') { i++; p->i = i; return p_unary(p); }
    p->i = i; return p_primary(p);
}
static double p_primary(struct P *p) {
    const char *s = p->s; int i = p->i;
    while (s[i] == ' ') i++;
    if (s[i] == '(') { i++; p->i = i; double v = p_expr(p); s = p->s; i = p->i; while (s[i] == ' ') i++; if (s[i] == ')') i++; p->i = i; return v; }
    if (s[i] >= 'a' && s[i] <= 'z') {
        char fn[8]; int n = 0;
        while ((s[i] >= 'a' && s[i] <= 'z') && n < 7) fn[n++] = s[i++];
        fn[n] = 0;
        while (s[i] == ' ') i++;
        double arg = 0;
        if (s[i] == '(') { i++; p->i = i; arg = p_expr(p); s = p->s; i = p->i; while (s[i] == ' ') i++; if (s[i] == ')') i++; p->i = i; }
        if (fn[0]=='s'&&fn[1]=='i'&&fn[2]=='n') return cm_sin(arg);
        if (fn[0]=='c'&&fn[1]=='o'&&fn[2]=='s') return cm_cos(arg);
        if (fn[0]=='t'&&fn[1]=='a'&&fn[2]=='n') return cm_tan(arg);
        if (fn[0]=='s'&&fn[1]=='q'&&fn[2]=='r'&&fn[3]=='t') return cm_sqrt(arg);
        if (fn[0]=='l'&&fn[1]=='n') return cm_ln(arg);
        if (fn[0]=='l'&&fn[1]=='o'&&fn[2]=='g') return cm_log(arg);
        if (fn[0]=='e'&&fn[1]=='x'&&fn[2]=='p') return cm_exp(arg);
        return 0;
    }
    double v = 0;
    if (s[i] == '.') { i++; double f = 0.1; while (s[i] >= '0' && s[i] <= '9') { v += (s[i]-'0') * f; f *= 0.1; i++; } p->i = i; return v; }
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
    if (s[i] == '.') { i++; double f = 0.1; while (s[i] >= '0' && s[i] <= '9') { v += (s[i]-'0') * f; f *= 0.1; i++; } }
    p->i = i; return v;
}
static int cal_is_func(const char *s) {
    const char *f[] = { "sin", "cos", "tan", "sqrt", "log", "ln", "exp" };
    for (int k = 0; k < 7; k++) { int j = 0; while (f[k][j] && f[k][j] == s[j]) j++; if (!f[k][j]) return 1; }
    return 0;
}
static void calc_key(struct wnd *w, const char *key) {
    if (key[0] == '=') {
        char buf[64]; int i;
        for (i = 0; i < w->calc_len && i < 63; i++) buf[i] = w->calc_expr[i];
        buf[i] = 0;
        struct P p; p.s = buf; p.i = 0;
        double r = p_expr(&p);
        fmt_double(buf, r);
        i = 0; while (buf[i] && i < 63) { w->calc_expr[i] = buf[i]; i++; }
        w->calc_len = i; w->calc_expr[i] = 0;
        return;
    }
    if (key[0] == 'C') { w->calc_len = 0; w->calc_expr[0] = 0; return; }
    if (key[0]=='D' && key[1]=='E' && key[2]=='L') { if (w->calc_len > 0) w->calc_len--; w->calc_expr[w->calc_len] = 0; return; }
    int i = w->calc_len;
    if (cal_is_func(key)) { const char *s = key; while (*s && i < 44) w->calc_expr[i++] = *s++; if (i < 46) w->calc_expr[i++] = '('; }
    else if (i < 46) w->calc_expr[i++] = key[0];
    w->calc_len = i; w->calc_expr[i] = 0;
}
static void draw_win_calc(struct wnd *w) {
    draw_window_title(w, "计算器");
    int cx = w->x, cy = w->y + TB;
    fill(cx, cy, w->w, w->h - TB, C_WHITE);
    fill(cx+8, cy+8, w->w-16, 26, C_LGRAY);
    rect(cx+8, cy+8, w->w-16, 26, C_BLACK);
    text(cx+12, cy+12, w->calc_expr, C_BLACK);
    for (int i = 0; i < w->nctrl; i++) {
        struct ctrl *c = &w->ctrls[i];
        if (c->type == CT_BUTTON) ctrl_draw_button(w, c);
    }
}

/* ---- 右键菜单 (与编辑器菜单共用) ---- */
#define MENU_MAX 8
static int menu_on = 0, menu_x = 0, menu_y = 0, menu_n = 0, menu_is_editor = 0;
struct menu_item { const char *label; int action; char file[32]; };
static struct menu_item menu_items[MENU_MAX];
#define MA_OPEN    10
#define MA_RUN     11
#define MA_DEL     12
#define MA_REFRESH 20
#define MA_TERM    21
#define MA_NEWTXT  22
#define MA_SYSINFO 23
#define MA_CTRL    24
#define MA_MKDIR   25
#define MA_TASKM   26
#define MA_SAVE    30
#define MA_NEWEDIT  31
#define MA_CLOSE   32
#define MA_CLEAR   33
#define MA_ABOUT   34
static void menu_clear(void) { menu_on = 0; menu_n = 0; menu_is_editor = 0; }
static void menu_add(const char *label, int action, const char *file) {
    if (menu_n >= MENU_MAX) return;
    menu_on = 1;
    menu_items[menu_n].label = label;
    menu_items[menu_n].action = action;
    int i = 0;
    while (file && file[i] && i < 31) { menu_items[menu_n].file[i] = file[i]; i++; }
    menu_items[menu_n].file[i] = 0;
    menu_n++;
}
static void draw_menu(void) {
    if (!menu_on) return;
    int mw = 140, mh = menu_n * 18 + 4;
    fill(menu_x, menu_y, mw, mh, C_LGRAY);
    rect(menu_x, menu_y, mw, mh, C_BLACK);
    for (int i = 0; i < menu_n; i++) {
        fill(menu_x+2, menu_y+2+i*18, mw-4, 16, C_LGRAY);
        text_cn(menu_x+6, menu_y+2+i*18, menu_items[i].label, C_BLACK);
    }
}
static int menu_hit(int x, int y) {
    if (!menu_on) return -1;
    int mw = 140, mh = menu_n * 18 + 4;
    if (x >= menu_x && x < menu_x+mw && y >= menu_y && y < menu_y+mh)
        return (y - menu_y - 2) / 18;
    return -1;
}

static void open_editor(const char *name, int load);
static void menu_exec(int idx);
static void open_ctrl_window(void);
static void open_dlg(const char *msg);

/* ---- 窗口操作 ---- */
static void redraw(void);
static void win_raise(struct wnd *w) {
    int idx = -1;
    for (int i = 0; i < norder; i++) if (&wins[order[i]] == w) { idx = i; break; }
    if (idx < 0) return;
    int tmp = order[idx];
    for (int i = idx; i < norder-1; i++) order[i] = order[i+1];
    order[norder-1] = tmp;
    order_update_active();
}
static void win_minimize(struct wnd *w) { w->minimized = 1; order_update_active(); redraw(); }
static void win_maximize(struct wnd *w) {
    if (w->maximized) {
        w->x=w->nx; w->y=w->ny; w->w=w->nw; w->h=w->nh; w->maximized=0;
    } else {
        w->nx=w->x; w->ny=w->y; w->nw=w->w; w->nh=w->h;
        w->x=0; w->y=24; w->w=g_w; w->h=g_h-24-TASKBAR_H; w->maximized=1;
    }
    redraw();
}
static struct wnd *open_window(int type, int x, int y, int w, int h) {
    if (norder >= MAXW) return 0;
    int i = -1;
    for (int k = 0; k < MAXW; k++) if (!wins[k].used) { i = k; break; }
    if (i < 0) return 0;
    wins[i].used = 1; wins[i].type = type;
    wins[i].nx=wins[i].x = x; wins[i].ny=wins[i].y = y;
    wins[i].nw=wins[i].w = w; wins[i].nh=wins[i].h = h;
    wins[i].minimized = wins[i].maximized = wins[i].drag = wins[i].resize = 0;
    wins[i].escroll = 0; wins[i].eview = EXPL_VIEW_LIST;
    wins[i].efile[0] = 0; wins[i].elen = 0; wins[i].escroll2 = 0;
    wins[i].edit_len = 0; wins[i].edit_path[0] = 0;
    wins[i].trow = 0; wins[i].cmdlen = 0; wins[i].cmd[0] = 0;
    wins[i].nctrl = 0; wins[i].cfocus = -1; wins[i].slider_drag = -1;
    wins[i].vscroll = 0; wins[i].sb_drag = 0;
    wins[i].esel = -1;
    wins[i].dlg_msg[0] = 0;
    wins[i].ren_from[0] = 0;
    wins[i].calc_len = 0; wins[i].calc_expr[0] = 0;
    for (int j = 0; j < T_HIST; j++) wins[i].thist[j][0] = 0;
    if (type == WT_TERM) { term_print(&wins[i], "XEKernelOS 终端"); term_print(&wins[i], "输入 HELP 查看命令"); }
    order[norder++] = i;
    order_update_active();
    return &wins[i];
}
static void close_window(int idx) {
    wins[idx].used = 0;
    for (int i = 0; i < norder; i++)
        if (order[i] == idx) {
            for (int j = i; j < norder-1; j++) order[j] = order[j+1];
            norder--; break;
        }
    order_update_active();
    redraw();
}
static void redraw(void) {
    draw_desktop_base();
    for (int k = 0; k < norder; k++)
        if (wins[order[k]].used && !wins[order[k]].minimized) draw_window(&wins[order[k]]);
    draw_menu();
}

static int win_hit_title(struct wnd *w,int x,int y){ return x>=w->x&&x<w->x+w->w&&y>=w->y&&y<w->y+TB; }
static int win_hit_xbtn(struct wnd *w,int x,int y){ return x>=wbtn_x(w,2)&&x<wbtn_x(w,2)+12&&y>=w->y&&y<w->y+TB; }
static int win_hit_maxbtn(struct wnd *w,int x,int y){ return x>=wbtn_x(w,1)&&x<wbtn_x(w,1)+12&&y>=w->y&&y<w->y+TB; }
static int win_hit_minbtn(struct wnd *w,int x,int y){ return x>=wbtn_x(w,0)&&x<wbtn_x(w,0)+12&&y>=w->y&&y<w->y+TB; }
static int win_contains(struct wnd *w,int x,int y){ return x>=w->x&&x<w->x+w->w&&y>=w->y&&y<w->y+w->h; }
/* 命中 resize 边缘: 0=无, 1=右, 2=下, 3=右下角 (8px 宽) */
static int win_hit_resize(struct wnd *w, int x, int y) {
    if (w->maximized) return 0;
    int R = 8;
    int hit_r = x >= w->x + w->w - R && x < w->x + w->w;
    int hit_b = y >= w->y + w->h - R && y < w->y + w->h;
    if (hit_r && hit_b) return 3;
    if (hit_r) return 1;
    if (hit_b) return 2;
    return 0;
}

/* 拖动/缩放: 重绘所有窗口消除拖影 */
static void drag_redraw_all(void) {
    for (int k = 0; k < norder; k++)
        if (wins[order[k]].used && !wins[order[k]].minimized) draw_window(&wins[order[k]]);
    draw_menu();
}

static void open_calc(void) {
    struct wnd *w = open_window(WT_CALC, 320, 120, 400, 240);
    if (!w) return;
    const char *keys[30] = {
        "C",  "(",  ")",  "/",  "^",  "sin",
        "7",  "8",  "9",  "*",  "cos", "log",
        "4",  "5",  "6",  "-",  "tan", "ln",
        "1",  "2",  "3",  "+",  "sqrt","exp",
        "0",  ".",  "=",  "DEL","",   ""
    };
    int bx = 10, by = 46, bw = 58, bh = 30, gap = 3;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 6; c++) {
            const char *k = keys[r * 6 + c];
            if (!k[0]) continue;
            wadd(w, CT_BUTTON, bx + c * (bw + gap), by + r * (bh + gap), bw, bh, 0, CAL_KEY, k);
        }
    redraw();
}
/* 任务管理器: SYS_TASK_LIST 拉取进程表 + SYS_KILL 强制结束 */
static void open_taskman(void) {
    struct wnd *w = open_window(WT_TASK, 180, 130, 420, 300);
    if (!w) return;
    w->esel = -1;
    wadd(w, CT_BUTTON, 0, 20, 68, 18, 0, CA_TKILL, "结束进程");
    wadd(w, CT_BUTTON, 0, 20, 68, 18, 0, CA_TKREF, "刷新");
    redraw();
}
static void open_editor(const char *name, int load) {
    struct wnd *ed = open_window(WT_EDIT, 300, 120, 460, 360);
    if (!ed) return;
    int i = 0; while (name && name[i] && i < 31) { ed->edit_path[i] = name[i]; i++; }
    ed->edit_path[i] = 0;
    ed->edit_len = 0;
    if (load && name) {
        int fd = _sys3(SYS_OPEN, (int)name, 0, 0);
        if (fd >= 0) {
            int r = _sys3(SYS_FREAD, fd, (int)ed->edit_buf, EDIT_MAX-1);
            _sys1(SYS_CLOSE, fd);
            if (r >= 0) { ed->edit_len = r; ed->edit_buf[r] = 0; }
        }
    }
    /* 文件名编辑框 + 保存按钮 (y 为窗口坐标: TB+18 与标签行对齐) */
    wadd(ed, CT_EDIT, 50, TB+18, 120, 16, 0, 0, ed->edit_path);
    wadd(ed, CT_BUTTON, 178, TB+18, 44, 16, 0, CA_SAVENOW, "保存");
    redraw();
}

static int expl_hit_file(struct wnd *w, int x, int y, dent *dout) {
    (void)x;
    int cyy = y - (w->y + TB);
    if (w->eview == EXPL_VIEW_FILE) return -1;
    if (cyy < 38) return -1;
    int idx = (cyy - 38) / 16 + w->vscroll;
    dent d[32];
    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
    if (n > 0 && idx < n) {
        dout->is_dir = d[idx].is_dir;
        dout->size = d[idx].size;
        int k = 0;
        while (d[idx].name[k] && k < 31) { dout->name[k] = d[idx].name[k]; k++; }
        dout->name[k] = 0;
        return idx;
    }
    return -1;
}
/* 新建文件夹: 找空闲名 NEWDIR/NEWDIR2.. */
static void expl_mkdir(struct wnd *w) {
    dent d[32];
    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
    char name[16];
    for (int t = 1; t < 100; t++) {
        if (t == 1) { name[0]='N';name[1]='E';name[2]='W';name[3]='D';name[4]='I';name[5]='R';name[6]=0; }
        else { name[0]='N';name[1]='E';name[2]='W';name[3]='D';name[4]='I';name[5]='R';
               name[6]='0'+t/10; name[7]='0'+t%10; name[8]=0; }
        int exists = 0;
        for (int i = 0; i < n; i++) if (str_ci(d[i].name, name)) { exists = 1; break; }
        if (!exists) { _sys1(SYS_FAT_MKDIR, (int)name); break; }
    }
    w->esel = -1;
    redraw();
}
/* 删除选中项: 目录 RMDIR, 文件 DEL */
static void expl_delsel(struct wnd *w) {
    if (w->esel < 0) return;
    dent d[32];
    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
    if (w->esel < n) {
        if (d[w->esel].is_dir) _sys1(SYS_FAT_RMDIR, (int)d[w->esel].name);
        else _sys1(SYS_FAT_DEL, (int)d[w->esel].name);
    }
    w->esel = -1;
    redraw();
}
static void open_explorer(void) {
    struct wnd *w = open_window(WT_EXPL, 60, 150, 400, 300);
    if (!w) return;
    wadd(w, CT_BUTTON, 180, TB+2, 68, 16, 0, CA_MKDIR, "新建文件夹");
    wadd(w, CT_BUTTON, 254, TB+2, 68, 16, 0, CA_RENAME, "重命名");
    wadd(w, CT_BUTTON, 326, TB+2, 68, 16, 0, CA_DELSEL, "删除");
    redraw();
}
/* 重命名对话框: 编辑框预填旧名, 确定后 SYS_FAT_RENAME
   (ctrl 坐标相对窗口原点 y=0=标题栏顶, 内容区从 y=TB 开始) */
static void open_rename(const char *oldname) {
    struct wnd *w = open_window(WT_DLG, 300, 220, 320, 130);
    if (!w) return;
    int i = 0; while (oldname && oldname[i] && i < 31) { w->ren_from[i] = oldname[i]; i++; }
    w->ren_from[i] = 0;
    i = 0; const char *m = "重命名:";
    while (m[i] && i < 63) { w->dlg_msg[i] = m[i]; i++; }
    w->dlg_msg[i] = 0;
    wadd(w, CT_EDIT, 10, TB + 36, 200, 16, 0, 0, oldname);     /* 标签行下方 */
    wadd(w, CT_BUTTON, 90, TB + 62, 60, 22, 0, CA_RENOK, "确定");
    redraw();
}
static void open_file(struct wnd *expl, const dent *d) {
    if (d->is_dir) {
        _sys1(SYS_FAT_CD, (int)d->name);
        expl->eview = EXPL_VIEW_LIST;
        redraw();
    } else if (is_exec_file(d->name)) {
        menu_clear();
        launch(d->name);
        redraw();
    } else if (is_text_file(d->name)) {
        menu_clear();
        open_editor(d->name, 1);
    } else {
        expl->eview = EXPL_VIEW_FILE;
        int fd = _sys3(SYS_OPEN, (int)d->name, 0, 0);
        if (fd >= 0) {
            int r = _sys3(SYS_FREAD, fd, (int)expl->ebuf, 2000);
            _sys1(SYS_CLOSE, fd);
            if (r >= 0) { expl->elen = r; expl->ebuf[r] = 0; }
            else expl->elen = 0;
        }
        redraw();
    }
}

static void menu_exec(int idx) {
    if (idx < 0 || idx >= menu_n) return;
    struct menu_item *it = &menu_items[idx];
    int action = it->action;
    int was_editor = menu_is_editor;
    menu_clear();
    if (was_editor) {
        struct wnd *ed = 0;
        if (active_win >= 0 && wins[active_win].used && wins[active_win].type == WT_EDIT) ed = &wins[active_win];
        if (action == MA_SAVE && ed) { _sys3(SYS_FAT_WRITE,(int)ed->edit_path,(int)ed->edit_buf,ed->edit_len); }
        else if (action == MA_NEWEDIT) { open_editor("NEWFILE.TXT", 0); }
        else if (action == MA_CLOSE && ed) { close_window(active_win); return; }
        else if (action == MA_CLEAR && ed) { ed->edit_len = 0; ed->edit_buf[0]=0; }
        else if (action == MA_ABOUT && ed) {
            const char *a="XEKernelOS 文本编辑器 v0.1";
            int i=0; while(a[i]&&ed->edit_len<EDIT_MAX-1){ed->edit_buf[ed->edit_len++]=a[i];i++;}
            ed->edit_buf[ed->edit_len]="\n"[0];
            if (ed->edit_len<EDIT_MAX-1){ed->edit_buf[ed->edit_len++]='\n';}
            i=0; while(a[i]&&ed->edit_len<EDIT_MAX-1){ed->edit_buf[ed->edit_len++]=a[i];i++;}
            ed->edit_buf[ed->edit_len]=0;
        }
        redraw();
        return;
    }
    if (action == MA_REFRESH) redraw();
    else if (action == MA_MKDIR) {
        for (int i = norder-1; i >= 0; i--)
            if (wins[order[i]].used && wins[order[i]].type == WT_EXPL) { expl_mkdir(&wins[order[i]]); break; }
    }
    else if (action == MA_TERM) { open_window(WT_TERM, 400, 150, 420, 260); redraw(); }
    else if (action == MA_NEWTXT) { open_editor("NEWFILE.TXT", 0); }
    else if (action == MA_SYSINFO) { open_window(WT_SYSINFO, 200, 150, 360, 260); redraw(); }
    else if (action == MA_TASKM) { open_taskman(); }
    else if (action == MA_RUN) { launch(it->file); }
    else if (action == MA_DEL) { _sys1(SYS_FAT_DEL, (int)it->file); redraw(); }
    else if (action == MA_OPEN) {
        struct wnd *expl = 0;
        for (int i = 0; i < norder; i++) if (wins[order[i]].type == WT_EXPL) { expl = &wins[order[i]]; break; }
        if (expl) {
            dent dn; dn.is_dir = 0; dn.size = 0;
            for (int k=0;k<32&&it->file[k];k++) dn.name[k]=it->file[k];
            dn.name[31]=0;
            open_file(expl, &dn);
        }
    }
    else if (action == MA_CTRL) { open_ctrl_window(); }
}

/* ---- 对话框 / 控件演示窗口 ---- */
static void open_dlg(const char *msg) {
    struct wnd *w = open_window(WT_DLG, 300, 200, 320, 140);
    if (!w) return;
    int i = 0; while (msg && msg[i] && i < 63) { w->dlg_msg[i] = msg[i]; i++; }
    w->dlg_msg[i] = 0;
    wadd(w, CT_BUTTON, (320-60)/2, 80, 60, 22, 0, CA_OK, "确定");
    redraw();
}
static void open_ctrl_window(void) {
    struct wnd *w = open_window(WT_CTRL, 200, 120, 440, 400);
    if (!w) return;
    wadd(w, CT_MENUBAR,  0,  0, 0, 16, 0, 0, "文件 编辑 帮助");
    wadd(w, CT_BUTTON,   10, 32, 90, 22, 0, CA_INFO,  "信息框");
    wadd(w, CT_BUTTON,  110, 32, 90, 22, 0, CA_PROGUP,"进度+");
    wadd(w, CT_BUTTON,  210, 32, 90, 22, 0, CA_PROGDN,"进度-");
    wadd(w, CT_PROGRESS, 10, 70, 220, 16, 40, 0, "");
    wadd(w, CT_SLIDER,   10, 96, 220, 16, 50, 0, "");
    wadd(w, CT_EDIT,     10, 122, 220, 16, 0, 0, "输入文字");
    wadd(w, CT_CHECK,    10, 150, 0, 14, 0, 0, "启用选项");
    wadd(w, CT_CHECK,    10, 170, 0, 14, 1, 0, "自动保存");
    wadd(w, CT_RADIO,    10, 196, 0, 14, 1, 0, "模式A");
    wadd(w, CT_RADIO,    10, 216, 0, 14, 0, 0, "模式B");
    redraw();
}

/* 控件事件: 命中控件返回索引, 否则 -1 */
static int ctrl_hit(struct wnd *w, int x, int y) {
    for (int i = 0; i < w->nctrl; i++) {
        struct ctrl *c = &w->ctrls[i];
        if (c->type == CT_MENUBAR) continue;   /* 菜单栏单独处理 */
        if (x >= w->x + c->x && x < w->x + c->x + (c->w ? c->w : 200) &&
            y >= w->y + c->y && y < w->y + c->y + c->h)
            return i;
    }
    return -1;
}
static int ctrl_hit_menubar(struct wnd *w, int x, int y) {
    for (int i = 0; i < w->nctrl; i++) {
        struct ctrl *c = &w->ctrls[i];
        if (c->type == CT_MENUBAR &&
            y >= w->y + c->y && y < w->y + c->y + c->h &&
            x >= w->x + c->x && x < w->x + w->w)
            return i;
    }
    return -1;
}
static void save_editor_now(struct wnd *ed) {
    if (!ed->edit_path[0]) { int i=0; const char*d="NEWFILE.TXT"; while(d[i]&&i<31){ed->edit_path[i]=d[i];i++;} ed->edit_path[i]=0; }
    _sys3(SYS_FAT_WRITE, (int)ed->edit_path, (int)ed->edit_buf, ed->edit_len);
    char m[48]; int i=0; const char*h="已保存: "; while(h[i]){m[i]=h[i];i++;}
    int j=0; while(ed->edit_path[j]&&i<47){m[i++]=ed->edit_path[j++];} m[i]=0;
    open_dlg(m);
}
static void menubar_click(struct wnd *w, int x) {
    /* 菜单栏弹菜单 */
    menu_clear();
    menu_x = x; menu_y = w->y + w->ctrls[0].y + 16;
    menu_add("新建文本", MA_NEWTXT, 0);
    menu_add("系统信息", MA_SYSINFO, 0);
    menu_add("任务管理器", MA_TASKM, 0);
    menu_add("控件演示", MA_CTRL, 0);
    redraw();
}
static void ctrl_press(struct wnd *w, int idx, int evx, int evy) {
    (void)evy;
    struct ctrl *c = &w->ctrls[idx];
    switch (c->type) {
        case CT_BUTTON:
            if (w->type == WT_TASK && c->id == CA_TKILL) {
                /* 强制结束选中进程: SIGKILL → SIG_DFL 默认动作即终止 */
                if (w->esel >= 0) {
                    struct tinfo t[TASK_MAX];
                    int n = _sys3(SYS_TASK_LIST, (int)t, TASK_MAX, 0);
                    if (n > 0 && w->esel < n) _sys2(SYS_KILL, (int)t[w->esel].pid, SIGKILL);
                }
                break;
            }
            if (w->type == WT_TASK && c->id == CA_TKREF) break;  /* 重绘即刷新 */
            if (w->type == WT_CALC && c->id == CAL_KEY) { calc_key(w, c->label); break; }
            if (w->type == WT_EXPL && c->id == CA_MKDIR) { expl_mkdir(w); break; }
            if (w->type == WT_EXPL && c->id == CA_DELSEL) { expl_delsel(w); break; }
            if (w->type == WT_EXPL && c->id == CA_RENAME) {
                if (w->esel >= 0) {
                    dent d[32];
                    int n = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
                    if (w->esel < n) open_rename(d[w->esel].name);
                }
                break;
            }
            if (w->type == WT_DLG && c->id == CA_RENOK && w->ren_from[0]) {
                char newname[24];
                int k = 0;
                while (k < 23 && w->ctrls[0].label[k]) { newname[k] = w->ctrls[0].label[k]; k++; }
                newname[k] = 0;
                if (k > 0) _sys2(SYS_FAT_RENAME, (int)w->ren_from, (int)newname);
                w->ren_from[0] = 0;
                close_window(active_win);
                redraw();
                break;
            }
            switch (c->id) {
                case CA_INFO: open_dlg("这是信息框\n点确定关闭"); break;
                case CA_PROGUP: {
                    for (int i = 0; i < w->nctrl; i++)
                        if (w->ctrls[i].type == CT_PROGRESS) { w->ctrls[i].val += 20; if (w->ctrls[i].val > 100) w->ctrls[i].val = 100; }
                    break; }
                case CA_PROGDN: {
                    for (int i = 0; i < w->nctrl; i++)
                        if (w->ctrls[i].type == CT_PROGRESS) { w->ctrls[i].val -= 20; if (w->ctrls[i].val < 0) w->ctrls[i].val = 0; }
                    break; }
                case CA_OK: close_window(active_win); break;
                case CA_SAVENOW: save_editor_now(w); break;
            }
            break;
        case CT_CHECK: c->val = c->val ? 0 : 1; break;
        case CT_RADIO:
            c->val = 1;
            for (int i = 0; i < w->nctrl; i++)
                if (w->ctrls[i].type == CT_RADIO && i != idx) w->ctrls[i].val = 0;
            break;
        case CT_EDIT: w->cfocus = idx; break;
        case CT_SLIDER:
            w->slider_drag = idx;
            c->val = (evx - (w->x + c->x)) * 100 / c->w;
            break;
    }
    redraw();
}

/* ---- 输入 ---- */
struct input_event { u32 type; i32 x, y; u32 arg; };
#define EV_MOVE 1
#define EV_BTN  2
#define EV_KEY  3
#define EV_WHEEL 4
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
static void sync_edit_path(struct wnd *w) {
    struct ctrl *c = &w->ctrls[0];
    int i = 0; while (c->label[i] && i < 31) { w->edit_path[i] = c->label[i]; i++; }
    w->edit_path[i] = 0;
}
static void handle_key(u8 sc) {
    struct wnd *foc = 0;
    if (active_win >= 0 && wins[active_win].used && !wins[active_win].minimized) {
        int t = wins[active_win].type;
        if (t == WT_TERM || t == WT_EDIT || t == WT_CTRL || t == WT_DLG) foc = &wins[active_win];
    }
    if (!foc) return;
    /* 编辑框控件输入优先 */
    int editbox = -1;
    if (foc->type == WT_CTRL || foc->type == WT_DLG) editbox = foc->cfocus;
    else if (foc->type == WT_EDIT && foc->cfocus == 0) editbox = 0;
    if (sc == 0x1C) {
        if (editbox >= 0) { foc->cfocus = -1; foc->slider_drag = -1; redraw(); return; }
        if (foc->type == WT_TERM) { term_exec(foc, foc->cmd); foc->cmdlen=0; foc->cmd[0]=0; }
        else if (foc->edit_len < EDIT_MAX-1) { foc->edit_buf[foc->edit_len++] = '\n'; foc->edit_buf[foc->edit_len]=0; }
        redraw(); return;
    }
    if (sc == 0x0E) {
        if (editbox >= 0) {
            struct ctrl *c = &foc->ctrls[editbox];
            int n = 0; while (c->label[n]) n++;
            bs_utf8(c->label, &n);
            c->label[n] = 0;
            if (foc->type == WT_EDIT) sync_edit_path(foc);
            redraw(); return;
        }
        if (foc->type == WT_TERM) bs_utf8(foc->cmd, &foc->cmdlen);
        else bs_utf8(foc->edit_buf, &foc->edit_len);
        redraw(); return;
    }
    if (sc == 0x2A || sc == 0x36) { sc_shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { sc_shift = 0; return; }
    if (sc >= 0x60) return;
    char c = sc_shift ? SC_SH[sc] : SC_LO[sc];
    if (!c || c < 32) return;
    if (editbox >= 0) {
        struct ctrl *ce = &foc->ctrls[editbox];
        int n = 0; while (ce->label[n]) n++;
        if (n < 23) { ce->label[n] = c; ce->label[n+1] = 0; }
        if (foc->type == WT_EDIT) sync_edit_path(foc);
        redraw(); return;
    }
    if (foc->type == WT_TERM) {
        if (foc->cmdlen < T_LINE-2) { foc->cmd[foc->cmdlen++]=c; foc->cmd[foc->cmdlen]=0; }
    } else {
        if (foc->edit_len < EDIT_MAX-1) { foc->edit_buf[foc->edit_len++]=c; foc->edit_buf[foc->edit_len]=0; }
    }
    redraw();
}

extern "C" __attribute__((section(".text.startup"))) void _start(void) {
    u32 info[5];
    _sys3(SYS_GETFB, (int)info, 0, 0);
    g_w = (int)info[1]; g_h = (int)info[2];
    if (g_w < 320 || g_h < 200) { g_w = 1024; g_h = 768; }
    int in_fd = _sys3(SYS_OPEN, (int)"/dev/input", 0, 0);
    if (in_fd < 0) { _sys1(SYS_EXIT, 1); }
    _sys3(SYS_TIME, (int)clock_buf, 0, 0);
    redraw();

    char last_sec = -1;
    input_event ev;
    int down = 0;
    for (;;) {
        int n = _sys3(SYS_FREAD, in_fd, (int)&ev, (int)sizeof(ev));
        if (n == (int)sizeof(ev)) {
            if (ev.type == EV_KEY) {
                if (ev.arg == 1 || ev.arg == 27) { _sys1(SYS_CLOSE, in_fd); _sys1(SYS_EXIT, 0); }
                handle_key((u8)ev.arg);
            }
            if (ev.type == EV_WHEEL) {
                int w = (i32)ev.arg;
                if (w && active_win >= 0 && wins[active_win].used && !wins[active_win].minimized) {
                    struct wnd *f = &wins[active_win];
                    int vis = scroll_visible(f);
                    if (vis > 0) {
                        f->vscroll += (w > 0) ? -3 : 3;
                        int total = win_total_lines(f);
                        int max = total - vis; if (max < 0) max = 0;
                        if (f->vscroll < 0) f->vscroll = 0;
                        if (f->vscroll > max) f->vscroll = max;
                        redraw();
                    }
                }
            }
            if (ev.type == EV_BTN) {
                int lbtn = ev.arg & 1;
                int rbtn = ev.arg & 2;
                if (rbtn && !lbtn) {
                    struct wnd *expl = 0;
                    for (int i = norder-1; i >= 0; i--) {
                        struct wnd *w = &wins[order[i]];
                        if (!w->used || w->minimized) continue;
                        if (w->type == WT_EXPL && win_contains(w, ev.x, ev.y)) { expl = w; break; }
                    }
                    if (expl && ev.y < g_h - TASKBAR_H) {
                        menu_clear();
                        dent df; int fi = expl_hit_file(expl, ev.x, ev.y, &df);
                        if (fi >= 0 && !df.is_dir) {
                            menu_x = ev.x; menu_y = ev.y;
                            menu_add("打开", MA_OPEN, df.name);
                            if (is_exec_file(df.name)) menu_add("运行", MA_RUN, df.name);
                            menu_add("删除", MA_DEL, df.name);
                        } else {
                            menu_x = ev.x; menu_y = ev.y;
                            menu_add("新建文件夹", MA_MKDIR, 0);
                            menu_add("刷新", MA_REFRESH, 0);
                            menu_add("系统信息", MA_SYSINFO, 0);
                            menu_add("任务管理器", MA_TASKM, 0);
                            menu_add("控件演示", MA_CTRL, 0);
                            menu_add("打开终端", MA_TERM, 0);
                        }
                        redraw();
                    } else if (ev.y < g_h - TASKBAR_H) {
                        menu_x = ev.x; menu_y = ev.y;
                        menu_add("刷新", MA_REFRESH, 0);
                        menu_add("系统信息", MA_SYSINFO, 0);
                        menu_add("任务管理器", MA_TASKM, 0);
                        menu_add("控件演示", MA_CTRL, 0);
                        menu_add("打开终端", MA_TERM, 0);
                        menu_add("新建文本", MA_NEWTXT, 0);
                        redraw();
                    }
                } else if (lbtn) {
                    down = 1;
                    int mi = menu_hit(ev.x, ev.y);
                    if (mi >= 0) { menu_exec(mi); redraw(); continue; }
                    else if (menu_on) { menu_clear(); }
                    int tb_y = g_h - TASKBAR_H;
                    int handled = 0;
                    if (ev.y >= tb_y && ev.y < tb_y + TASKBAR_H) {
                        for (int i = norder-1; i >= 0; i--) {
                            struct wnd *w = &wins[order[i]];
                            if (!w->used) continue;
                            if (ev.x >= w->tbtn_x && ev.x < w->tbtn_x + 96) {
                                if (w->minimized) w->minimized = 0;
                                win_raise(w); redraw(); handled = 1; break;
                            }
                        }
                        if (!handled) handled = 1;
                    }
                    if (!handled) {
                        for (int i = norder-1; i >= 0; i--) {
                            struct wnd *w = &wins[order[i]];
                            if (!w->used || w->minimized) continue;
                            /* resize 边缘命中优先于滚动条 (右缘 8px), 否则点边框会被滚动条抢走 */
                            int rs = win_hit_resize(w, ev.x, ev.y);
                            if (rs && !win_hit_title(w, ev.x, ev.y)) {
                                w->resize = 1; w->drag = 0; w->rdir = rs;
                                win_raise(w); handled=1; break;
                            }
                            /* 滚动条命中 (右边缘 8px 已留给 resize) */
                            if (w->type != WT_DLG && w->type != WT_SYSINFO && w->type != WT_CTRL &&
                                ev.y >= w->y + TB &&
                                ev.x >= w->x + w->w - SBW - 8 && ev.x < w->x + w->w - 8) {
                                int vis = scroll_visible(w);
                                if (vis > 0) { w->sb_drag = 1; scroll_set(w, ev.y, vis); redraw(); handled=1; break; }
                            }
                            if (win_hit_xbtn(w, ev.x, ev.y)) { close_window(order[i]); handled=1; break; }
                            if (win_hit_maxbtn(w, ev.x, ev.y)) { win_maximize(w); handled=1; break; }
                            if (win_hit_minbtn(w, ev.x, ev.y)) { win_minimize(w); handled=1; break; }
                            if (win_hit_title(w, ev.x, ev.y)) {
                                if (!w->maximized) { w->drag=1; w->resize=0; w->dx=ev.x-w->x; w->dy=ev.y-w->y; }
                                win_raise(w); redraw(); handled=1; break;
                            }
                            if (win_contains(w, ev.x, ev.y)) {
                                win_raise(w);
                                if (w->nctrl > 0) {
                                    int mbi = ctrl_hit_menubar(w, ev.x, ev.y);
                                    if (mbi >= 0) { menubar_click(w, ev.x); handled=1; break; }
                                    int hi = ctrl_hit(w, ev.x, ev.y);
                                    if (hi >= 0) { ctrl_press(w, hi, ev.x, ev.y); handled=1; break; }
                                }
                                if (w->type == WT_EXPL) {
                                    int cyy = ev.y - (w->y + TB);
                                    if (cyy >= 22 && cyy < 22+16) {
                                        _sys1(SYS_FAT_CD, (int)"..");
                                        w->eview = EXPL_VIEW_LIST; redraw();
                                    } else if (cyy >= 38) {
                                        dent d[32];
                                        int nn = _sys3(SYS_VFS_LIST, (int)"", (int)d, 32);
                                        int idx = (cyy - 38) / 16 + w->vscroll;
                                        if (nn > 0 && idx < nn) {
                                            if (w->esel == idx) open_file(w, &d[idx]);   /* 二次点击打开 */
                                            else { w->esel = idx; redraw(); }             /* 首次点击选中 */
                                        }
                                    }
                                } else if (w->type == WT_TASK) {
                                    int cyy = ev.y - (w->y + TB);
                                    if (cyy >= 24) {
                                        int idx = (cyy - 24) / 16 + w->vscroll;
                                        struct tinfo t[TASK_MAX];
                                        int nn = _sys3(SYS_TASK_LIST, (int)t, TASK_MAX, 0);
                                        if (nn > 0 && idx < nn) { w->esel = idx; redraw(); }
                                    }
                                } else if (w->type == WT_EDIT) {
                                    int cx0 = w->x, cy0 = w->y + TB;
                                    int cyy = ev.y - cy0;
                                    if (cyy >= 0 && cyy < 16) {
                                        /* 菜单栏 */
                                        if (ev.x >= cx0+2 && ev.x < cx0+40) {
                                            menu_is_editor = 1; menu_x = cx0+2; menu_y = cy0+18;
                                            menu_add("新建", MA_NEWEDIT, 0);
                                            menu_add("保存", MA_SAVE, 0);
                                            menu_add("关闭", MA_CLOSE, 0);
                                            redraw();
                                        } else if (ev.x >= cx0+42 && ev.x < cx0+80) {
                                            menu_is_editor = 1; menu_x = cx0+42; menu_y = cy0+18;
                                            menu_add("清空", MA_CLEAR, 0);
                                            redraw();
                                        } else if (ev.x >= cx0+82 && ev.x < cx0+120) {
                                            menu_is_editor = 1; menu_x = cx0+82; menu_y = cy0+18;
                                            menu_add("关于", MA_ABOUT, 0);
                                            redraw();
                                        }
                                    }
                                }
                                handled=1; break;
                            }
                        }
                    }
                    if (!handled) {
                        for (int i = 0; i < N_ICONS; i++) {
                            int x = icon_x(i), y = icon_y();
                            if (ev.x >= x-2 && ev.x < x+ICON_SZ+2 && ev.y >= y-2 && ev.y < y+ICON_SZ+2) {
                                if (ICONS[i].act == I_EXPL) open_explorer();
                                else if (ICONS[i].act == I_TERM) open_window(WT_TERM, 460, 150, 420, 260);
                                else if (ICONS[i].act == I_EDIT) open_editor("NEWFILE.TXT", 0);
                                else if (ICONS[i].act == I_CALC) open_calc();
                                else if (ICONS[i].act == I_TASK) open_taskman();
                                else launch(ICONS[i].file);
                                redraw();
                                break;
                            }
                        }
                    }
                } else {
                    down = 0;
                    for (int i = 0; i < norder; i++) {
                        struct wnd *w = &wins[order[i]];
                        if (!w->used) continue;
                        if (w->drag || w->resize) { w->drag = 0; w->resize = 0; redraw(); break; }
                        if (w->slider_drag >= 0) { w->slider_drag = -1; redraw(); break; }
                        if (w->sb_drag) { w->sb_drag = 0; redraw(); break; }
                    }
                }
            }
            if (ev.type == EV_MOVE && down) {
                for (int i = 0; i < norder; i++) {
                    struct wnd *w = &wins[order[i]];
                    if (!w->used) continue;
                    if (w->drag) {
                        int oldx = w->x, oldy = w->y;
                        w->x = ev.x - w->dx;
                        w->y = ev.y - w->dy;
                        if (w->y < 24) w->y = 24;
                        if (w->y + w->h > g_h - TASKBAR_H) w->y = g_h - TASKBAR_H - w->h;
                        fill(oldx-2, oldy-2, w->w+4, w->h+4, C_BLUE);
                        drag_redraw_all();
                        break;
                    } else if (w->resize && !w->maximized) {
                        int ow = w->w, oh = w->h;
                        int nw = w->w, nh = w->h;
                        if (w->rdir == 1 || w->rdir == 3) nw = ev.x - w->x;
                        if (w->rdir == 2 || w->rdir == 3) nh = ev.y - w->y;
                        if (nw < 200) nw = 200;
                        if (nh < 120) nh = 120;
                        if (w->x + nw > g_w - 4) nw = g_w - 4 - w->x;
                        if (w->y + nh > g_h - TASKBAR_H) nh = g_h - TASKBAR_H - w->y;
                        w->w = nw; w->h = nh;
                        w->nw = nw; w->nh = nh;
                        /* 缩小时清除旧区域再重绘, 防止残留 */
                        if (nw < ow || nh < oh)
                            fill(w->x - 3, w->y - 3, ow + 7, oh + 7, C_BLUE);
                        drag_redraw_all();
                        break;
                    } else if (w->sb_drag) {
                        int vis = scroll_visible(w);
                        if (vis > 0) { scroll_set(w, ev.y, vis); redraw(); }
                        break;
                    } else if (w->slider_drag >= 0 && w->slider_drag < w->nctrl) {
                        struct ctrl *c = &w->ctrls[w->slider_drag];
                        if (c->type == CT_SLIDER) {
                            int v = (ev.x - (w->x + c->x)) * 100 / c->w;
                            if (v < 0) v = 0; if (v > 100) v = 100;
                            c->val = v;
                            redraw();
                        }
                        break;
                    }
                }
            }
        } else {
            _sys1(SYS_SLEEP, 12);
            /* 光标闪烁: ~0.5s 翻转一次, 仅重绘聚焦窗口 (编辑器正文/编辑框) */
            static int blink_cnt = 0;
            if (++blink_cnt >= 40) {
                blink_cnt = 0;
                blink_on = !blink_on;
                if (active_win >= 0 && wins[active_win].used && !wins[active_win].minimized) {
                    struct wnd *f = &wins[active_win];
                    if (f->type == WT_EDIT || f->cfocus >= 0)
                        draw_window(f);
                }
            }
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