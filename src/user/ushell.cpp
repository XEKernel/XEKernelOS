/* XEKernelOS User-Space Shell — Ring3 */

#include "usys.h"

#define COLOR_BLACK   0x00
#define COLOR_DGRAY   0x08
#define COLOR_LGRAY   0x07
#define COLOR_WHITE   0x0F
#define COLOR_LRED    0x0C
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_YELLOW  0x0E

/* ---- helpers ---- */

static int strcmp_ci(const char *a, const char *b) {
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
        a++; b++;
    }
}

static const char *next_token(const char *buf, char *out) {
    while (*buf == ' ') buf++;
    int i = 0;
    while (*buf && *buf != ' ' && i < 31) out[i++] = *buf++;
    out[i] = 0;
    while (*buf == ' ') buf++;
    return buf;
}

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

static void cmd_help(void) {
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts("Ring3 Shell — 命令列表\n");
    gfx_puts("────────────────────────────\n");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts("  HELP      帮助\n");
    gfx_puts("  CLEAR     清屏\n");
    gfx_puts("  LS        列出文件\n");
    gfx_puts("  CD  <dir> 切换目录\n");
    gfx_puts("  CAT <file>查看文件\n");
    gfx_puts("  ECHO <msg>回显消息\n");
    gfx_puts("  MKDIR <n> 创建目录\n");
    gfx_puts("  RMDIR <n> 删除目录\n");
    gfx_puts("  RM   <f>  删除文件\n");
    gfx_puts("  MV <a> <b> 重命名\n");
    gfx_puts("  CP <a> <b> 复制文件\n");
    gfx_puts("  CREATE <n> <c> 创建文件\n");
    gfx_puts("  TIME      显示时间\n");
    gfx_puts("  EXIT      退出\n");
}

static void cmd_ls(void)  { fat_dir(); }
static void cmd_cls(void) { gfx_cls(COLOR_BLACK); }

static void ok_msg(void) {
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts(" 完成\n");
    gfx_set_fg(COLOR_LGRAY);
}
static void err_msg(const char *s) {
    gfx_set_fg(COLOR_LRED);
    gfx_puts(s);
    gfx_set_fg(COLOR_LGRAY);
}

static void cmd_mkdir(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: MKDIR <目录名>\n"); return; }
    int r = fat_mkdir(args);
    if (r == 0) ok_msg();
    else err_msg(" 创建失败\n");
}

static void cmd_rmdir(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: RMDIR <目录名>\n"); return; }
    int r = fat_rmdir(args);
    if (r == 0) ok_msg();
    else err_msg(" 删除失败（不存在或非空）\n");
}

static void cmd_rm(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: RM <文件名>\n"); return; }
    if (fat_delete(args) == 0) ok_msg();
    else err_msg(" 文件不存在\n");
}

static void cmd_mv(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: MV <原名> <新名>\n"); return; }
    char src[32]; int i = 0;
    while (*args && *args != ' ' && i < 31) src[i++] = *args++;
    src[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: MV <原名> <新名>\n"); return; }
    if (fat_rename(src, args) == 0) ok_msg();
    else err_msg(" 重命名失败\n");
}

static void cmd_cp(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: CP <源> <目标>\n"); return; }
    char src[32]; int i = 0;
    while (*args && *args != ' ' && i < 31) src[i++] = *args++;
    src[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: CP <源> <目标>\n"); return; }
    static char cpbuf[1024] __attribute__((section(".data"))) = {0};
    int sz = fat_read(src, cpbuf, 1023);
    if (sz <= 0) { err_msg(" 源文件不存在\n"); return; }
    if (fat_write(args, cpbuf, sz) == 0) ok_msg();
    else err_msg(" 写入失败\n");
}

static void cmd_create(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: CREATE <文件名> <内容>\n"); return; }
    char name[32]; int i = 0;
    while (*args && *args != ' ' && i < 31) name[i++] = *args++;
    name[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: CREATE <文件名> <内容>\n"); return; }
    int len = 0; while (args[len]) len++;
    if (fat_write(name, args, len) == 0) ok_msg();
    else err_msg(" 创建失败\n");
}

static void cmd_time(void) {
    char tbuf[9];
    sys_time(tbuf);
    gfx_set_fg(COLOR_YELLOW);
    gfx_puts(tbuf);
    gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

static void cmd_cd(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { fat_cd("\\"); return; }
    if (fat_cd(args)) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts(" 目录不存在\n");
        gfx_set_fg(COLOR_LGRAY);
    }
}

static void cmd_cat(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts(" 用法: CAT <文件名>\n");
        gfx_set_fg(COLOR_LGRAY);
        return;
    }
    static char fbuf[1024] __attribute__((section(".data"))) = {0};
    int sz = fat_read(args, fbuf, 1023);
    if (sz <= 0) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts(" 文件不存在\n");
        gfx_set_fg(COLOR_LGRAY);
        return;
    }
    fbuf[sz] = 0;
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts(fbuf);
    gfx_putc('\n');
}

static void cmd_echo(const char *args) {
    while (*args == ' ') args++;
    if (*args) gfx_puts(args);
    gfx_putc('\n');
}

/* ---- main ---- */

extern "C" void _start(void) {
    volatile char buf[128];
    char cmd[32];

    gfx_cls(COLOR_BLACK);
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts("XEKernelOS 用户态 Shell\n");
    gfx_set_fg(COLOR_DGRAY);
    gfx_puts("输入 HELP 查看命令\n\n");

    for (;;) {
        prompt();

        kb_read((char *)buf, (int)(sizeof(buf) - 1));

        int n = 0;
        while (n < 127 && buf[n]) n++;
        if (n == 0) continue;

        const char *args = next_token((const char *)buf, cmd);

        if (!strcmp_ci(cmd, "HELP")) {
            cmd_help();
        } else if (!strcmp_ci(cmd, "CLEAR")) {
            cmd_cls();
        } else if (!strcmp_ci(cmd, "LS")) {
            cmd_ls();
        } else if (!strcmp_ci(cmd, "CD")) {
            cmd_cd(args);
        } else if (!strcmp_ci(cmd, "CAT")) {
            cmd_cat(args);
        } else if (!strcmp_ci(cmd, "ECHO")) {
            cmd_echo(args);
        } else if (!strcmp_ci(cmd, "MKDIR")) {
            cmd_mkdir(args);
        } else if (!strcmp_ci(cmd, "RMDIR")) {
            cmd_rmdir(args);
        } else if (!strcmp_ci(cmd, "RM")) {
            cmd_rm(args);
        } else if (!strcmp_ci(cmd, "MV")) {
            cmd_mv(args);
        } else if (!strcmp_ci(cmd, "CP")) {
            cmd_cp(args);
        } else if (!strcmp_ci(cmd, "CREATE")) {
            cmd_create(args);
        } else if (!strcmp_ci(cmd, "TIME")) {
            cmd_time();
        } else if (!strcmp_ci(cmd, "EXIT")) {
            break;
        } else {
            gfx_set_fg(COLOR_LRED);
            gfx_puts(" 未知命令: ");
            gfx_puts(cmd);
            gfx_puts("\n");
            gfx_set_fg(COLOR_LGRAY);
        }
    }

    proc_exit();
}
