/* XEKernelOS User-Space Shell — Ring3
   准则五: 只读 FS 操作使用 ufs (用户态 FAT12 库)
   写操作暂用内核 syscall */

#include "usys.h"
#include "ufs.h"

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

/* Find '>' in args; if found, split args at '>' and return filename.
   On return, args_out points to everything before '>', filename gets the rest. */
static int parse_redirect(const char *args, const char **args_out, char *filename) {
    const char *p = args;
    while (*p) {
        if (*p == '>' && (p == args || p[-1] == ' ')) {
            /* Copy args before '>' to a static buffer */
            static char pre[64] __attribute__((section(".data")));
            int i = 0;
            const char *q = args;
            while (q < p && i < 63) { pre[i++] = *q++; }
            pre[i] = 0;
            *args_out = pre;

            /* Skip '>' and spaces, copy filename */
            p++;
            while (*p == ' ') p++;
            int j = 0;
            while (*p && *p != ' ' && *p != '>' && j < 31) filename[j++] = *p++;
            filename[j] = 0;
            return 1;
        }
        p++;
    }
    *args_out = args;
    return 0;
}

static void prompt(void) {
    char cwd[64];
    ufs_cwd(cwd, 64);
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
    gfx_puts("  HELP       帮助\n");
    gfx_puts("  CLEAR      清屏\n");
    gfx_puts("  LS         列出文件\n");
    gfx_puts("  CD   <dir> 切换目录\n");
    gfx_puts("  CAT  <file>查看文件\n");
    gfx_puts("  ECHO <msg> 回显消息\n");
    gfx_puts("  MKDIR <n>  创建目录\n");
    gfx_puts("  RMDIR <n>  删除目录\n");
    gfx_puts("  RM   <f>   删除文件\n");
    gfx_puts("  MV <a> <b> 重命名\n");
    gfx_puts("  CP <a> <b> 复制文件\n");
    gfx_puts("  CREATE <n> <c> 创建文件\n");
    gfx_puts("  RUN  <file>运行批处理\n");
    gfx_puts("  TMP  L     列出/tmp\n");
    gfx_puts("  TMP  W <n> <c> 写入/tmp\n");
    gfx_puts("  TMP  R <n> 读取/tmp\n");
    gfx_puts("  TMP  D <n> 删除/tmp\n");
    gfx_puts("  TIME       显示时间\n");
    gfx_puts("  >   file   输出重定向\n");
    gfx_puts("  EXIT       退出\n");
}

static void cmd_ls(void)  { ufs_ls(); }
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
    int sz = ufs_read_file(src, cpbuf, 1023);
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

static void cmd_tmp(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: TMP L|W <n> <c>|R <n>|D <n>\n"); return; }

    char op[4];
    const char *rest = next_token(args, op);

    if (!strcmp_ci(op, "L")) {
        static char lbuf[512] __attribute__((section(".data"))) = {0};
        int n = rd_list(lbuf, 511);
        if (n <= 0) {
            gfx_puts("  /tmp 为空\n");
            return;
        }
        lbuf[n] = 0;
        gfx_set_fg(COLOR_LCYAN);
        gfx_puts("  /tmp:\n");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts(lbuf);

    } else if (!strcmp_ci(op, "W")) {
        while (*rest == ' ') rest++;
        if (!*rest) { err_msg(" 用法: TMP W <文件名> <内容>\n"); return; }
        char name[32]; int i = 0;
        while (*rest && *rest != ' ' && i < 31) name[i++] = *rest++;
        name[i] = 0;
        while (*rest == ' ') rest++;
        if (!*rest) { err_msg(" 用法: TMP W <文件名> <内容>\n"); return; }
        int len = 0; while (rest[len]) len++;
        if (rd_create(name, rest, len) == 0) ok_msg();
        else err_msg(" ramdisk 已满\n");

    } else if (!strcmp_ci(op, "R")) {
        while (*rest == ' ') rest++;
        if (!*rest) { err_msg(" 用法: TMP R <文件名>\n"); return; }
        static char rbuf[4096] __attribute__((section(".data"))) = {0};
        int n = rd_read(rest, rbuf, 4095);
        if (n < 0) { err_msg(" 文件不存在\n"); return; }
        rbuf[n] = 0;
        gfx_puts(rbuf);
        gfx_putc('\n');

    } else if (!strcmp_ci(op, "D")) {
        while (*rest == ' ') rest++;
        if (!*rest) { err_msg(" 用法: TMP D <文件名>\n"); return; }
        if (rd_remove(rest) == 0) ok_msg();
        else err_msg(" 文件不存在\n");

    } else {
        err_msg(" 未知操作\n");
    }
}

static void cmd_cd(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { ufs_cd("\\"); return; }
    if (ufs_cd(args)) {
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
    int sz = ufs_read_file(args, fbuf, 1023);
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

/* Open a new FD for write-only output capture.
   Returns the FD, or -1 on failure. */
static int open_output_fd(void) {
    /* Allocate a blank FD for output capture.
       We open README.TXT (always exists) then override the buffer. */
    int fd = sys_open("README.TXT");
    if (fd < 0) return -1;
    /* Reset position to 0 for overwrite */
    sys_lseek(fd, 0, 0);
    return fd;
}

/* Redirect command output to a file, then write to disk. */
static int redirect_to(const char *filename, void (*cmd_fn)(const char *), const char *args) {
    int fd = open_output_fd();
    if (fd < 0) { err_msg(" 无法创建输出缓冲区\n"); return -1; }

    /* Redirect output to FD */
    int oldfd = sys_set_outfd(fd);

    /* Execute command — all gfx_puts go to fd_buf[fd] */
    cmd_fn(args);

    /* Restore screen output */
    sys_set_outfd(oldfd);

    /* Write accumulated buffer to disk */
    int r = sys_fsync(fd, filename);
    if (r < 0) { err_msg(" 写入文件失败\n"); }
    sys_close(fd);
    return r;
}

/* Run a batch script file line by line */
static void cmd_run(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { err_msg(" 用法: RUN <脚本文件>\n"); return; }
    static char sbuf[2048] __attribute__((section(".data"))) = {0};
    int sz = ufs_read_file(args, sbuf, 2047);
    if (sz <= 0) { err_msg(" 脚本文件不存在\n"); return; }
    sbuf[sz] = 0;

    gfx_set_fg(COLOR_YELLOW);
    gfx_puts("执行脚本: ");
    gfx_puts(args);
    gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);

    /* Execute each line */
    int pos = 0;
    volatile char line[128];
    char lcmd[32];
    while (pos < sz) {
        /* Skip leading whitespace and blank lines */
        while (pos < sz && (sbuf[pos] == ' ' || sbuf[pos] == '\r' || sbuf[pos] == '\n')) pos++;
        if (pos >= sz) break;

        /* Read one line */
        int li = 0;
        while (pos < sz && sbuf[pos] != '\r' && sbuf[pos] != '\n' && li < 127)
            line[li++] = sbuf[pos++];
        line[li] = 0;
        if (li == 0) continue;

        /* Echo the line being executed */
        gfx_puts("  > ");
        gfx_puts((const char *)line);
        gfx_putc('\n');

        /* Dispatch (inline copy of main loop dispatch) */
        const char *rargs = next_token((const char *)line, lcmd);

        if (!strcmp_ci(lcmd, "HELP")) {
            cmd_help();
        } else if (!strcmp_ci(lcmd, "CLEAR")) {
            cmd_cls();
        } else if (!strcmp_ci(lcmd, "LS")) {
            cmd_ls();
        } else if (!strcmp_ci(lcmd, "CD")) {
            cmd_cd(rargs);
        } else if (!strcmp_ci(lcmd, "CAT")) {
            cmd_cat(rargs);
        } else if (!strcmp_ci(lcmd, "ECHO")) {
            cmd_echo(rargs);
        } else if (!strcmp_ci(lcmd, "MKDIR")) {
            cmd_mkdir(rargs);
        } else if (!strcmp_ci(lcmd, "RMDIR")) {
            cmd_rmdir(rargs);
        } else if (!strcmp_ci(lcmd, "RM")) {
            cmd_rm(rargs);
        } else if (!strcmp_ci(lcmd, "MV")) {
            cmd_mv(rargs);
        } else if (!strcmp_ci(lcmd, "CP")) {
            cmd_cp(rargs);
        } else if (!strcmp_ci(lcmd, "CREATE")) {
            cmd_create(rargs);
        } else if (!strcmp_ci(lcmd, "TIME")) {
            cmd_time();
        } else if (!strcmp_ci(lcmd, "TMP")) {
            cmd_tmp(rargs);
        } else if (!strcmp_ci(lcmd, "ECHO") || !strcmp_ci(lcmd, "REM") || lcmd[0] == '#') {
            /* comment or handled above */
        } else if (!strcmp_ci(lcmd, "EXIT")) {
            break;
        }
    }

    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("脚本执行完毕\n");
    gfx_set_fg(COLOR_LGRAY);
}

/* ---- main ---- */

extern "C" void _start(void) {
    volatile char buf[128];
    char cmd[32];

    gfx_cls(COLOR_BLACK);
    ufs_init(); /* 准则五: init user-space FAT12 */
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

        /* Check for redirect */
        char redir_file[32];
        const char *real_args;
        int has_redir = parse_redirect(args, &real_args, redir_file);

        if (!strcmp_ci(cmd, "HELP")) {
            if (has_redir) redirect_to(redir_file, (void(*)(const char*))cmd_help, "");
            else cmd_help();
        } else if (!strcmp_ci(cmd, "CLEAR")) {
            cmd_cls();  /* no redirect */
        } else if (!strcmp_ci(cmd, "LS")) {
            if (has_redir) redirect_to(redir_file, (void(*)(const char*))cmd_ls, "");
            else cmd_ls();
        } else if (!strcmp_ci(cmd, "CD")) {
            cmd_cd(real_args);
        } else if (!strcmp_ci(cmd, "CAT")) {
            if (has_redir) redirect_to(redir_file, cmd_cat, real_args);
            else cmd_cat(real_args);
        } else if (!strcmp_ci(cmd, "ECHO")) {
            if (has_redir) redirect_to(redir_file, cmd_echo, real_args);
            else cmd_echo(real_args);
        } else if (!strcmp_ci(cmd, "MKDIR")) {
            cmd_mkdir(real_args);
        } else if (!strcmp_ci(cmd, "RMDIR")) {
            cmd_rmdir(real_args);
        } else if (!strcmp_ci(cmd, "RM")) {
            cmd_rm(real_args);
        } else if (!strcmp_ci(cmd, "MV")) {
            cmd_mv(real_args);
        } else if (!strcmp_ci(cmd, "CP")) {
            cmd_cp(real_args);
        } else if (!strcmp_ci(cmd, "CREATE")) {
            cmd_create(real_args);
        } else if (!strcmp_ci(cmd, "RUN")) {
            cmd_run(real_args);
        } else if (!strcmp_ci(cmd, "TMP")) {
            cmd_tmp(real_args);
        } else if (!strcmp_ci(cmd, "TIME")) {
            if (has_redir) redirect_to(redir_file, (void(*)(const char*))cmd_time, "");
            else cmd_time();
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
