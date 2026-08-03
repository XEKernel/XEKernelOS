/* Demo user program — uses _SYSCALL macros directly */
#pragma clang diagnostic ignored "-Wunused-function"
#include "libc.h"

extern "C" void _start(void) {
    int r;
    _SYSCALL4(SYS_WRITE, (int)"=== XEKernelOS Demo ===\n", 23, 0, r);

    /* Try opening README.TXT */
    int fd;
    _SYSCALL1(SYS_OPEN, (int)"README.TXT", fd);
    if (fd >= 0) {
        _SYSCALL4(SYS_WRITE, (int)"--- README ---\n", 15, 0, r);
        char buf[256];
        int n;
        _SYSCALL4(SYS_FREAD, fd, (int)buf, 255, n);
        if (n > 0) {
            buf[n] = 0;
            int len = 0; while (buf[len]) len++;
            _SYSCALL4(SYS_WRITE, (int)buf, len, 0, r);
        }
        _SYSCALL4(SYS_WRITE, (int)"\n", 1, 0, r);
        _SYSCALL1(SYS_CLOSE, fd, r);
    } else {
        _SYSCALL4(SYS_WRITE, (int)"file not found\n", 16, 0, r);
    }

    /* Test stat */
    int info[4];
    int sr;
    _SYSCALL4(SYS_STAT, (int)"HELLO.TXT", (int)info, 0, sr);
    _SYSCALL4(SYS_WRITE, (int)"HELLO.TXT size: ", 16, 0, r);
    if (sr >= 0) {
        /* Print size as hex for simplicity */
        for (int sh = 28; sh >= 0; sh -= 4) {
            char c = "0123456789ABCDEF"[(info[0] >> sh) & 0xF];
            _SYSCALL4(SYS_WRITE, (int)&c, 1, 0, r);
        }
    } else {
        _SYSCALL4(SYS_WRITE, (int)"err", 3, 0, r);
    }
    _SYSCALL4(SYS_WRITE, (int)"\nDone.\n", 8, 0, r);

    _SYSCALL0(SYS_EXIT, r);
}
