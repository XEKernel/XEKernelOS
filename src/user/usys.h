#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_READ  3
#define SYS_OPEN  4
#define SYS_FREAD 5
#define SYS_SBRK  6
#define SYS_GETCWD 7
#define SYS_TIME   8
#define SYS_GETFB  9
#define SYS_CLOSE  10
#define SYS_MOUSE  11
#define SYS_SLEEP  12
#define SYS_CLS    13
#define SYS_GFX_PUTC  14
#define SYS_GFX_PUTS  15
#define SYS_GFX_SET_FG 16

static inline int syscall4(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3));
    return ret;
}

static inline int syscall1(int num, int a1) { return syscall4(num, a1, 0, 0); }
static inline int syscall0(int num)          { return syscall4(num, 0, 0, 0); }

/* ---- graphics wrappers ---- */
static inline void gfx_putc(char c)        { syscall1(SYS_GFX_PUTC, (int)c); }
static inline void gfx_puts(const char *s) { syscall1(SYS_GFX_PUTS, (int)s); }
static inline void gfx_set_fg(int color)   { syscall1(SYS_GFX_SET_FG, color); }
static inline void gfx_cls(int color)      { syscall1(SYS_CLS, color); }

/* ---- input ---- */
static inline int  kb_read(char *buf, int max) { return syscall4(SYS_READ, (int)buf, max, 0); }

/* ---- misc ---- */
static inline void proc_exit(void)              { syscall0(SYS_EXIT); }
static inline int  gfx_getfb(unsigned int *buf) { return syscall1(SYS_GETFB, (int)buf); }

#ifdef __cplusplus
}
#endif
