/* XEKernelOS User-Space Shell — Ring3 */

#include "usys.h"

/* ---- minimal string helpers (no libc) ---- */
static int  strlen_u8(const char *s) { int n=0; while(s[n]) n++; return n; }
static int  strcmp_ci(const char *a, const char *b) {
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
        a++; b++;
    }
}

static void prompt(void) {
    gfx_set_fg(0x0A);  // LGREEN
    gfx_puts("user");
    gfx_set_fg(0x0B);  // LCYAN
    gfx_puts("@ring3");
    gfx_set_fg(0x0A);
    gfx_puts("> ");
    gfx_set_fg(0x07);  // LGRAY
}

static void cmd_help(void) {
    gfx_set_fg(0x0B);
    gfx_puts("Ring3 Shell:\n");
    gfx_set_fg(0x07);
    gfx_puts("  HELP     - 帮助\n");
    gfx_puts("  CLS      - 清屏\n");
    gfx_puts("  ECHO <x> - 回显文本\n");
    gfx_puts("  EXIT     - 返回内核\n");
}

static void put_hex(int v) {
    static const char h[] = "0123456789ABCDEF";
    char b[8]; int i = 7;
    b[i--] = 0;
    do { b[i--] = h[v & 15]; v >>= 4; } while (v);
    gfx_puts(b + i + 1);
}

static void cmd_echo(const char *s) {
    while (*s == ' ') s++;
    if (*s) { gfx_puts(s); gfx_puts(" ("); put_hex(strlen_u8(s)); gfx_puts(" chars)"); }
}

extern "C" void _start(void) {
    char buf[128];

    gfx_cls(0x00);
    gfx_set_fg(0x0B);
    gfx_puts("XEKernelOS 用户态 Shell — Ring 3\n");
    gfx_set_fg(0x08);
    gfx_puts("输入 HELP 查看命令，EXIT 退出。\n");

    for (;;) {
        prompt();
        int n = kb_read(buf, sizeof(buf) - 1);
        buf[n] = 0;

        gfx_putc('\n');

        if (n == 0) continue;

        if (!strcmp_ci(buf, "HELP"))
            cmd_help();
        else if (!strcmp_ci(buf, "CLS"))
            gfx_cls(0x00);
        else if (!strcmp_ci(buf, "EXIT"))
            break;
        else if (buf[0]=='E' && buf[1]=='C' && buf[2]=='H' && buf[3]=='O' && (!buf[4] || buf[4]==' '))
            cmd_echo(buf + 4);
        else {
            gfx_set_fg(0x0C);  // LRED
            gfx_puts("未知命令。\n");
        }
    }

    proc_exit();
}
