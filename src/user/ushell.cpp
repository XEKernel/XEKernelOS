/* XEKernelOS User-Space Shell — Ring3
 * Primary interactive interface for daily use.
 * Kernel shell handles low-level debugging only.
 */

#include "usys.h"

/* ---- color constants (match kernel gfx) ---- */
#define COLOR_BLACK   0x00
#define COLOR_DGRAY   0x08
#define COLOR_LGRAY   0x07
#define COLOR_WHITE   0x0F
#define COLOR_LRED    0x0C
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_YELLOW  0x0E

/* ---- minimal string helpers (no libc) ---- */
static int strlen_u8(const char *s) { int n=0; while(s[n]) n++; return n; }

static int strcmp_ci(const char *a, const char *b) {
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
        a++; b++;
    }
}

static int strncmp_ci(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

static void skip_spaces(const char **s) {
    while (**s == ' ') (*s)++;
}

/* ---- prompt with current directory ---- */
static void prompt(void) {
    char cwd[64];
    fat_cwd(cwd, 64);
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("user");
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts(cwd);
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("> ");
    gfx_set_fg(COLOR_LGRAY);
}

/* ---- commands ---- */

static void __attribute__((noinline)) cmd_help(void) {
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts("Ring3 Shell:\n");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts("  HELP      帮助\n");
    gfx_puts("  CLS       清屏\n");
    gfx_puts("  LS/DIR    列出文件\n");
    gfx_puts("  CAT <f>   查看文件\n");
    gfx_puts("  CD  <dir> 切换目录\n");
    gfx_puts("  MKDIR <d> 创建目录\n");
    gfx_puts("  RMDIR <d> 删除空目录\n");
    gfx_puts("  CREATE <f> <内容>  创建文件\n");
    gfx_puts("  RM  <f>   删除文件\n");
    gfx_puts("  MV  <旧> <新>  重命名\n");
    gfx_puts("  CP  <源> <目标>  复制文件\n");
    gfx_puts("  ECHO <x>  回显\n");
    gfx_puts("  TIME      时间\n");
    gfx_puts("  EXIT      返回内核\n");
}

static void cmd_ls(void) {
    fat_dir();
}

static void cmd_cat(const char *name) {
    skip_spaces(&name);
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: CAT <文件名>\n"); gfx_set_fg(COLOR_LGRAY); return; }

    /* Read entire file into buffer */
    static char fbuf[4096];
    int sz = fat_read(name, fbuf, sizeof(fbuf) - 1);
    if (sz < 0) { gfx_set_fg(COLOR_LRED); gfx_puts("文件未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    fbuf[sz] = 0;
    gfx_puts(fbuf);
    gfx_putc('\n');
}

static void cmd_cd(const char *name) {
    skip_spaces(&name);
    if (!*name) { fat_cd("\\"); return; }
    int r = fat_cd(name);
    if (r) { gfx_set_fg(COLOR_LRED); gfx_puts("未找到。\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_mkdir(const char *name) {
    skip_spaces(&name);
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: MKDIR <目录>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int r = fat_mkdir(name);
    if (r == -1) { gfx_set_fg(COLOR_LRED); gfx_puts("已存在。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -2) { gfx_set_fg(COLOR_LRED); gfx_puts("磁盘已满。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r)       { gfx_set_fg(COLOR_LRED); gfx_puts("失败。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_rmdir(const char *name) {
    skip_spaces(&name);
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: RMDIR <目录>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int r = fat_rmdir(name);
    if (r == -1) { gfx_set_fg(COLOR_LRED); gfx_puts("未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -2) { gfx_set_fg(COLOR_LRED); gfx_puts("目录非空。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_create(const char *args) {
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: CREATE <文件名> <内容>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    /* Extract filename */
    char fname[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) fname[i++] = *args++;
    fname[i] = 0;
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: CREATE <文件名> <内容>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int sz = strlen_u8(args);
    if (fat_write(fname, args, sz) == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else                                 { gfx_set_fg(COLOR_LRED);   gfx_puts("失败\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_rm(const char *name) {
    skip_spaces(&name);
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: RM <文件名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (fat_delete(name) == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else                       { gfx_set_fg(COLOR_LRED);   gfx_puts("未找到。\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_mv(const char *args) {
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: MV <原名> <新名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    char src[64]; int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = 0;
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: MV <原名> <新名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (fat_rename(src, args) == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else                             { gfx_set_fg(COLOR_LRED);   gfx_puts("未找到。\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_cp(const char *args) {
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: CP <源文件> <目标>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    char src[64]; int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = 0;
    skip_spaces(&args);
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts("用法: CP <源文件> <目标>\n"); gfx_set_fg(COLOR_LGRAY); return; }

    static char cpbuf[4096];
    int sz = fat_read(src, cpbuf, 4096);
    if (sz < 0) { gfx_set_fg(COLOR_LRED); gfx_puts("源文件未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (fat_write(args, cpbuf, sz) == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else                                 { gfx_set_fg(COLOR_LRED);   gfx_puts("复制失败。\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_echo(const char *s) {
    skip_spaces(&s);
    if (*s) gfx_puts(s);
    gfx_putc('\n');
}

static void cmd_time(void) {
    char t[9];
    sys_time(t);
    gfx_set_fg(COLOR_YELLOW);
    gfx_puts(t);
    gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

/* ---- entry point ---- */

extern "C" void _start(void) {
    char buf[128];

    syscall4(1 /*SYS_WRITE*/, (int)"S", 1, 0);  /* start */

    gfx_cls(COLOR_BLACK);
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts("XEKernelOS 用户态 Shell\n");
    gfx_set_fg(COLOR_DGRAY);
    gfx_puts("输入 HELP 查看命令，EXIT 返回内核。\n\n");

    for (;;) {
        syscall4(1, (int)"P", 1, 0);  /* prompt */
        prompt();
        int n = kb_read(buf, sizeof(buf) - 1);
        syscall4(1, (int)"R", 1, 0);  /* read done */
        buf[n] = 0;

        if (n == 0) { syscall4(1, (int)"Z", 1, 0); continue; }

        /* ---- match commands ---- */
        if (!strcmp_ci(buf, "HELP")) {
            syscall4(1, (int)"H", 1, 0);  /* help match */
            cmd_help();
        } else if (!strcmp_ci(buf, "CLS") || !strcmp_ci(buf, "CLEAR"))
            gfx_cls(COLOR_BLACK);
        else if (!strcmp_ci(buf, "LS") || !strcmp_ci(buf, "DIR"))
            cmd_ls();
        else if (!strcmp_ci(buf, "EXIT"))
            break;
        else if (!strcmp_ci(buf, "TIME"))
            cmd_time();

        /* CD */
        else if (!strcmp_ci(buf, "CD"))
            cmd_cd("\\");
        else if (!strncmp_ci(buf, "CD ", 3))
            cmd_cd(buf + 3);

        /* CAT */
        else if (!strcmp_ci(buf, "CAT"))
            cmd_cat("");
        else if (!strncmp_ci(buf, "CAT ", 4))
            cmd_cat(buf + 4);

        /* MKDIR */
        else if (!strcmp_ci(buf, "MKDIR"))
            cmd_mkdir("");
        else if (!strncmp_ci(buf, "MKDIR ", 6))
            cmd_mkdir(buf + 6);

        /* RMDIR */
        else if (!strcmp_ci(buf, "RMDIR"))
            cmd_rmdir("");
        else if (!strncmp_ci(buf, "RMDIR ", 6))
            cmd_rmdir(buf + 6);

        /* CREATE */
        else if (!strcmp_ci(buf, "CREATE"))
            cmd_create("");
        else if (!strncmp_ci(buf, "CREATE ", 7))
            cmd_create(buf + 7);

        /* RM */
        else if (!strcmp_ci(buf, "RM"))
            cmd_rm("");
        else if (!strncmp_ci(buf, "RM ", 3))
            cmd_rm(buf + 3);

        /* MV / REN */
        else if (!strcmp_ci(buf, "MV"))
            cmd_mv("");
        else if (!strncmp_ci(buf, "MV ", 3))
            cmd_mv(buf + 3);
        else if (!strcmp_ci(buf, "REN"))
            cmd_mv("");
        else if (!strncmp_ci(buf, "REN ", 4))
            cmd_mv(buf + 4);

        /* CP / COPY */
        else if (!strcmp_ci(buf, "CP"))
            cmd_cp("");
        else if (!strncmp_ci(buf, "CP ", 3))
            cmd_cp(buf + 3);

        /* ECHO */
        else if (!strncmp_ci(buf, "ECHO", 4)) {
            const char *a = buf + 4;
            skip_spaces(&a);
            cmd_echo(a);
        }

        else {
            syscall4(1, (int)"U", 1, 0);  /* unknown */
            gfx_set_fg(COLOR_LRED);
            gfx_puts("未知命令。输入 HELP 查看帮助。\n");
            gfx_set_fg(COLOR_LGRAY);
        }
    }

    proc_exit();
}
