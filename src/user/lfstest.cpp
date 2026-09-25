/* LFSTEST.BIN — 长文件名 (VFAT LFN) 读侧验证程序

   1. SYS_VFS_LIST 列根目录 → 把每个文件名打印到串口
      (长名/中文名应显示为 UTF-8, 而不是 8.3 短名或方块)
   2. 用长名打开文件并回读正文 (验证 LFN → 短项的名字匹配)
   3. 用 8.3 短名打开同一个文件 (验证短名回退匹配)
   串口可断言, 因此本程序是 LFN 读侧的唯一自动化证据。

   注: sys_write 每次调用后会自动补 '\n', 故所有输出都先拼进
   line[] 再整行写出, 否则每个数字都会独占一行。 */

#include "usys.h"

struct dent { int is_dir; unsigned int size; char name[32]; };

static char line[608];
static int  lp = 0;

static void lc(char c) { if (lp < (int)sizeof(line) - 2) line[lp++] = c; }
static void ls(const char *s) { while (*s) lc(*s++); }
static void li(int v) {
    char b[12]; int i = 0;
    if (v < 0) { lc('-'); v = -v; }
    if (!v) b[i++] = '0';
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) lc(b[--i]);
}
static void lflush(void) {
    syscall4(SYS_WRITE, (int)line, lp);
    lp = 0;
}

extern "C" void _start(void) {
    static struct dent d[32];
    static char buf[512];

    ls("LFSTEST: list"); lflush();
    int n = syscall4(SYS_VFS_LIST, (int)"", (int)d, 32);
    ls("LFSTEST: count="); li(n); lflush();
    for (int i = 0; i < n && i < 32; i++) {
        ls(d[i].is_dir ? "  [D] " : "  [F] ");
        ls(d[i].name);
        ls("  size="); li((int)d[i].size);
        lflush();
    }

    /* 长名 (含中文) 打开 */
    int fd = syscall1(SYS_OPEN, (int)"中文文档.txt");
    ls("LFSTEST: open-long fd="); li(fd); lflush();
    if (fd >= 0) {
        int r = syscall4(SYS_FREAD, fd, (int)buf, 500);
        ls("LFSTEST: read="); li(r); lflush();
        if (r > 0) { buf[r] = 0; ls("LFSTEST: body: "); ls(buf); lflush(); }
        syscall1(SYS_CLOSE, fd);
    }

    /* 短名 8.3 回退 (同一个文件) */
    int fd2 = syscall1(SYS_OPEN, (int)"LONGFI~1.TXT");
    ls("LFSTEST: open-short fd="); li(fd2); lflush();
    if (fd2 >= 0) {
        int r2 = syscall4(SYS_FREAD, fd2, (int)buf, 500);
        ls("LFSTEST: short-read="); li(r2); lflush();
        if (r2 > 0) { buf[r2] = 0; ls("LFSTEST: short-body: "); ls(buf); lflush(); }
        syscall1(SYS_CLOSE, fd2);
    }

    ls("LFSTEST: done"); lflush();
    syscall1(SYS_EXIT, 0);
    for (;;) {}
}
