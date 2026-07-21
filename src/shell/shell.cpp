#include "shell/shell.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/ata.h"
#include "drivers/mouse.h"
#include "kernel/mm.h"
#include "kernel/user.h"
#include "kernel/loader.h"
#include "fs/fat12.h"
#include "lib/ports.h"
#include "lib/strutil.h"

#include "drivers/serial.h"
#include "kernel/paging.h"
#include "user/ushell_blob.h"

// (keep other includes)

static char buf[CMD_BUF];

static u32 parse_u32(const char *s) {
    u32 v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static u32 parse_hex(const char *s) {
    u32 v = 0;
    for (;;) {
        char c = *s;
        if      (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
        else break;
        s++;
    }
    return v;
}

static char hex_buf[512];

static void hex_dump(u32 lba, const u8 *data) {
    gfx_set_fg(COLOR_YELLOW);
    gfx_puts("LBA "); gfx_put_hex_u32(lba); gfx_puts(":\n");
    for (int row = 0; row < 32; row++) {
        gfx_set_fg(COLOR_DGRAY);
        gfx_put_hex_byte((row * 16) >> 8);
        gfx_put_hex_byte((row * 16) & 0xFF);
        gfx_puts(": ");
        gfx_set_fg(COLOR_WHITE);
        for (int col = 0; col < 16; col++) {
            gfx_put_hex_byte(data[row * 16 + col]);
            gfx_putc(' ');
            if (col == 7) gfx_putc(' ');
        }
        gfx_putc(' ');
        gfx_set_fg(COLOR_LGRAY);
        for (int col = 0; col < 16; col++) {
            char c = data[row * 16 + col];
            gfx_putc((c >= 32 && c < 127) ? c : '.');
        }
        gfx_putc('\n');
    }
    gfx_set_fg(COLOR_LGRAY);
}

static void cmd_help(void) {
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts_utf8("可用命令:\n");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("  HELP        帮助\n");
    gfx_puts_utf8("  CLEAR       清屏\n");
    gfx_puts_utf8("  INFO        系统信息\n");
    gfx_puts_utf8("  MEM         内存统计\n");
    gfx_puts_utf8("  DISK ID     磁盘识别\n");
    gfx_puts_utf8("  DISK R      读扇区\n");
    gfx_puts_utf8("  DISK W      写字节\n");
    gfx_puts_utf8("  LS          列出文件\n");
    gfx_puts_utf8("  CAT         查看文件\n");
    gfx_puts_utf8("  CD          切换目录\n");
    gfx_puts_utf8("  MKDIR       创建目录\n");
    gfx_puts_utf8("  RMDIR       删除目录\n");
    gfx_puts_utf8("  CP          复制文件\n");
    gfx_puts_utf8("  MV          重命名\n");
    gfx_puts_utf8("  RM          删除文件\n");
    gfx_puts_utf8("  CREATE      创建文件\n");
    gfx_puts_utf8("  MOUSE       鼠标坐标\n");
    gfx_puts_utf8("  RUN         运行程序\n");
    gfx_puts_utf8("  ECHO        回显\n");
    gfx_puts_utf8("  REBOOT      重启\n");
    gfx_puts_utf8("  SHUTDOWN    关机\n");
    gfx_puts_utf8("  USERSH      用户态 Shell\n");
}

static void cmd_info(void) {
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts_utf8("XEKernelOS v0.2.0 - XEKernel\n");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("架构: x86 32 位保护模式\n");
    gfx_puts_utf8("启动: MBR -> Stage2 -> 内核\n");
    gfx_puts_utf8("分页: 4KB 页 标识映射\n");
    gfx_puts_utf8("文件系统: FAT12 | 定时器: PIT 100Hz\n");
    gfx_puts_utf8("输入: 键盘中断 + PS/2 鼠标\n");
    gfx_puts_utf8("安全: Ring 3 + TSS\n");
}

static void cmd_mem(void) {
    char s[32];
    u32 total = mm_free_count();
    gfx_puts_utf8("空闲页: ");
    int n = 0, t = total;
    if (t == 0) { s[0] = '0'; n = 1; }
    else { while (t) { s[n++] = '0' + (t % 10); t /= 10; } }
    for (int i = 0; i < n/2; i++) { char c = s[i]; s[i] = s[n-1-i]; s[n-1-i] = c; }
    s[n] = '\n'; s[n+1] = 0;
    gfx_puts(s);
}

static void cmd_disk_id(void) {
    u16 ident[256];
    int r = ata_identify(ident);
    if (r == -1)      { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA: 无磁盘。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -2)      { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA 设备忙。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -3)      { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATAPI 设备（非硬盘）。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -4)      { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA 识别错误。\n"); gfx_set_fg(COLOR_LGRAY); return; }

    char model[41];
    for (int i = 0; i < 20; i++) {
        u16 w = ident[27 + i];
        model[i*2]     = (w >> 8) & 0xFF;
        model[i*2 + 1] = w & 0xFF;
    }
    model[40] = 0;
    for (int i = 39; i > 0 && model[i] == ' '; i--) model[i] = 0;

    char sn[21];
    for (int i = 0; i < 10; i++) {
        u16 w = ident[10 + i];
        sn[i*2]     = (w >> 8) & 0xFF;
        sn[i*2 + 1] = w & 0xFF;
    }
    sn[20] = 0;
    for (int i = 19; i > 0 && sn[i] == ' '; i--) sn[i] = 0;

    u32 sectors = ((u32)ident[61] << 16) | ident[60];

    gfx_set_fg(COLOR_DGRAY); gfx_puts_utf8("型号:     ");
    gfx_set_fg(COLOR_WHITE);  gfx_puts(model); gfx_putc('\n');
    gfx_set_fg(COLOR_DGRAY); gfx_puts_utf8("序列号:   ");
    gfx_set_fg(COLOR_WHITE);  gfx_puts(sn);    gfx_putc('\n');
    gfx_set_fg(COLOR_DGRAY); gfx_puts_utf8("扇区数:   ");
    gfx_set_fg(COLOR_YELLOW); gfx_put_hex_u32(sectors); gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

static void cmd_disk_read(const char *args) {
    u32 lba = parse_u32(args);
    int r = ata_read(lba, 1, (u16 *)hex_buf);
    if (r) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA 读错误。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    hex_dump(lba, (u8 *)hex_buf);
}

static void cmd_disk_write(const char *args) {
    while (*args == ' ') args++;
    u32 lba = parse_u32(args);
    while (*args >= '0' && *args <= '9') args++;
    while (*args == ' ') args++;
    u32 off = parse_hex(args);
    while ((*args >= '0' && *args <= '9') || (*args >= 'a' && *args <= 'f') || (*args >= 'A' && *args <= 'F')) args++;
    while (*args == ' ') args++;
    u8 val = (u8)parse_hex(args);
    if (off >= 512) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("偏移错。\n"); gfx_set_fg(COLOR_LGRAY); return; }

    int r = ata_read(lba, 1, (u16 *)hex_buf);
    if (r) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA 读错误。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    hex_buf[off] = val;
    r = ata_write(lba, 1, (const u16 *)hex_buf);
    if (r) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("ATA 写错误。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts_utf8("已写入 0x"); gfx_put_hex_byte(val); gfx_puts_utf8(" 于 LBA "); gfx_put_hex_u32(lba); gfx_puts("+0x"); gfx_put_hex_byte((u8)off); gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

static void cmd_echo(const char *s) { if (s) gfx_puts(s); gfx_putc('\n'); }

static void cmd_ls(void) {
    fat_dir();
}

static void cmd_cd(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { fat_cd("\\"); return; }
    int r = fat_cd(name);
    if (r == -1) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    /* CD success: no output, prompt path updates automatically */
}

static void cmd_mkdir(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: MKDIR <名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int r = fat_mkdir(name);
    if (r == -1) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("已存在。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -2) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("磁盘已满。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r)       { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("创建目录失败。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_cat(const char *name) {
    while (*name == ' ') name++;
    if (!*name) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: CAT <文件名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int r = fat_cat(name);
    if (r != 0) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("文件未找到。\n"); gfx_set_fg(COLOR_LGRAY); }
    else gfx_putc('\n');
}

static void cmd_mouse(void) {
    int x, y, btn;
    mouse_get(&x, &y, &btn);
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts_utf8("鼠标: X="); gfx_put_hex_u32(x); 
    gfx_puts(" Y="); gfx_put_hex_u32(y);
    gfx_puts(" BTN="); gfx_put_hex_u32(btn); gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

static const char ring3_msg[] = "Ring3: Hello!";

static void user_loop(void) {
    u32 addr = (u32)ring3_msg;
    __asm__ volatile(
        "mov $1, %%eax\n"
        "mov %0, %%ebx\n"
        "mov $14, %%ecx\n"
        "int $0x80\n"
        : : "r"(addr) : "eax", "ebx", "ecx"
    );
    for (;;) __asm__ volatile("pause");
}

static void cmd_run(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts_utf8("用法: RUN <文件名>\n");
        gfx_set_fg(COLOR_LGRAY);
        return;
    }
    if (load_binary(args, args) != 0) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts_utf8("程序加载失败。\n");
        gfx_set_fg(COLOR_LGRAY);
    }
}

static void cmd_create(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: CREATE <名> <内容>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    char fname[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) fname[i++] = *args++;
    fname[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: CREATE <名> <内容>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    u32 len = 0;
    while (args[len]) len++;
    int r = fat_write_file(fname, (const u8 *)args, len);
    if (r == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else        { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("失败\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_rm(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: RM <文件名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (fat_delete_file(args) == 0) { gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY); }
    else                            { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("未找到。\n"); gfx_set_fg(COLOR_LGRAY); }
}

static void cmd_rmdir(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: RMDIR <目录>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    int r = fat_rmdir(args);
    if (r == -1) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (r == -2) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("目录非空。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_cp(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: CP <源> <目标>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    char src[64]; int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: CP <源> <目标>\n"); gfx_set_fg(COLOR_LGRAY); return; }

    /* read source file */
    static u8 copybuf[4096];
    int sz = fat_read_file_buf(src, copybuf, 4096);
    if (sz < 0) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("源文件未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    if (sz > 4096) sz = 4096;

    int r = fat_write_file(args, copybuf, (u32)sz);
    if (r != 0) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("复制失败。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_mv(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: MV <原名> <新名>\n"); gfx_set_fg(COLOR_LGRAY); return; }
    char src[64]; int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = 0;
    while (*args == ' ') args++;
    if (!*args) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("用法: MV <原名> <新名>\n"); gfx_set_fg(COLOR_LGRAY); return; }

    int r = fat_rename(src, args);
    if (r != 0) { gfx_set_fg(COLOR_LRED); gfx_puts_utf8("未找到。\n"); gfx_set_fg(COLOR_LGRAY); return; }
    gfx_set_fg(COLOR_LGREEN); gfx_puts_utf8("成功\n"); gfx_set_fg(COLOR_LGRAY);
}

static void cmd_reboot(void) {
    gfx_set_fg(COLOR_YELLOW);
    gfx_puts_utf8("重启中...\n");
    gfx_set_fg(COLOR_LGRAY);
    outb(0x64, 0xFE);
    __asm__ volatile("0: hlt; jmp 0b");
}

static void cmd_shutdown(void) {
    gfx_set_fg(COLOR_YELLOW);
    gfx_puts_utf8("已停机。\n");
    gfx_set_fg(COLOR_LGRAY);
    __asm__ volatile("0: hlt; jmp 0b");
}

static void cmd_usersh(void) {
    /* Copy embedded user-shell binary to user-space load address, then
     * enter Ring3.  On exit (SYS_EXIT), shell_loop is resumed. */
    u8 *dst = (u8 *)0x400000;
    for (u32 i = 0; i < ushell_blob_len; i++)
        dst[i] = ushell_blob[i];

    gfx_clear(COLOR_BLACK);
    PagingManager *user_pd = new PagingManager();
    enter_user_mode(0x400000, 0, user_pd, 0, nullptr);
    shell_redraw();
    gfx_putc('\n');
}

static u32 shell_esp;

void shell_save_esp(void) {
    __asm__ volatile("movl %%esp, %0" : "=m"(shell_esp));
}

void shell_recover(registers_t *r) {
    r->eip    = (u32)shell_loop;
    r->cs     = 0x18;
    r->_esp   = shell_esp;
    r->eflags = 0x202;
    r->eax    = 0;
}

void shell_redraw(void) {
    /* Clear framebuffer and redraw shell prompt */
    gfx_clear(COLOR_BLACK);
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("XEKernel");
    gfx_set_fg(COLOR_LCYAN);
    char cwd[32];
    fat_cwd_str(cwd, 32);
    gfx_puts(cwd);
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("> ");
    gfx_set_fg(COLOR_LGRAY);
    gfx_cursor_draw();
}

void shell_loop(void) {
    shell_save_esp();
    __asm__ volatile("sti");  /* enable interrupts → PIT fires → mcursor_update */
    for (;;) {
        gfx_set_fg(COLOR_LGREEN);
        gfx_puts("XEKernel");
        gfx_set_fg(COLOR_LCYAN);
        {
            char cwd[32];
            fat_cwd_str(cwd, 32);
            gfx_puts(cwd);
        }
        gfx_set_fg(COLOR_LGREEN);
        gfx_puts("> ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_cursor_draw();
        gfx.mcursor_update();
        kb_readline(buf, CMD_BUF);
        gfx_cursor_erase();
        if (!buf[0]) continue;

        if (!strcmp_ci(buf, "HELP"))
            cmd_help();
        else if (!strcmp_ci(buf, "CLEAR"))
            gfx_clear(0x00);
        else if (!strcmp_ci(buf, "INFO"))
            cmd_info();
        else if (!strcmp_ci(buf, "MEM"))
            cmd_mem();
        else if (!strcmp_ci(buf, "DISK ID"))
            cmd_disk_id();
        else if (!strncmp_ci(buf, "DISK R", 6))
            cmd_disk_read(buf + 6);
        else if (!strncmp_ci(buf, "DISK W", 6))
            cmd_disk_write(buf + 6);
        else if (!strcmp_ci(buf, "LS"))
            cmd_ls();
        else if (!strncmp_ci(buf, "CD ", 3))
            cmd_cd(buf + 3);
        else if (!strcmp_ci(buf, "CD"))
            cmd_cd("\\");
        else if (!strcmp_ci(buf, "MKDIR"))
            cmd_mkdir("");
        else if (!strncmp_ci(buf, "MKDIR ", 6))
            cmd_mkdir(buf + 6);
        else if (!strcmp_ci(buf, "CAT"))
            cmd_cat("");
        else if (!strncmp_ci(buf, "CAT ", 4))
            cmd_cat(buf + 4);
        else if (!strcmp_ci(buf, "MOUSE"))
            cmd_mouse();
        else if (!strcmp_ci(buf, "RUN"))
            cmd_run("");
        else if (!strncmp_ci(buf, "RUN ", 4))
            cmd_run(buf + 4);
        else if (!strcmp_ci(buf, "CREATE"))
            cmd_create("");
        else if (!strncmp_ci(buf, "CREATE ", 7))
            cmd_create(buf + 7);
        else if (!strcmp_ci(buf, "RM"))
            cmd_rm("");
        else if (!strncmp_ci(buf, "RM ", 3))
            cmd_rm(buf + 3);
        else if (!strncmp_ci(buf, "ECHO", 4)) {
            const char *a = buf + 4;
            while (*a == ' ') a++;
            cmd_echo(a);
        }
        else if (!strcmp_ci(buf, "RMDIR"))
            cmd_rmdir("");
        else if (!strncmp_ci(buf, "RMDIR ", 6))
            cmd_rmdir(buf + 6);
        else if (!strcmp_ci(buf, "CP"))
            cmd_cp("");
        else if (!strncmp_ci(buf, "CP ", 3))
            cmd_cp(buf + 3);
        else if (!strcmp_ci(buf, "MV"))
            cmd_mv("");
        else if (!strncmp_ci(buf, "MV ", 3))
            cmd_mv(buf + 3);
        else if (!strcmp_ci(buf, "REBOOT"))
            cmd_reboot();
        else if (!strcmp_ci(buf, "SHUTDOWN"))
            cmd_shutdown();
        else if (!strcmp_ci(buf, "USERSH"))
            cmd_usersh();
        else {
            gfx_set_fg(COLOR_LRED);
            gfx_puts_utf8("未知命令。输入 HELP 查看帮助。\n");
            gfx_set_fg(COLOR_LGRAY);
        }
    }
}
