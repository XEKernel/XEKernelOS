/* XEKernelOS User-Space libc — uses raw syscall macros to avoid
   inline-asm return-value loss under clang -O1. */
#pragma once
#include "usys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw syscall macros — capture return value into caller-provided variable.
   Using a macro forces the compiler to assign to a named lvalue, preventing
   the optimizer from discarding the asm output. */
#define _SYSCALL4(num, a1, a2, a3, retvar) \
    __asm__ volatile("int $0x80" \
        : "=a"(retvar) \
        : "a"(num), "b"(a1), "c"(a2), "d"(a3) \
        : "memory")
#define _SYSCALL1(num, a1, retvar)  _SYSCALL4(num, a1, 0, 0, retvar)
#define _SYSCALL0(num, retvar)      _SYSCALL4(num, 0, 0, 0, retvar)

/* ---- raw wrappers that write result to caller's variable ---- */
static inline int raw_open(const char *n, int *fd)  { _SYSCALL1(SYS_OPEN,  (int)n, *fd); return *fd; }
static inline int raw_read(int fd, char *b, int m, int *n) { _SYSCALL4(SYS_FREAD, fd, (int)b, m, *n); return *n; }
static inline int raw_close(int fd, int *r)          { _SYSCALL1(SYS_CLOSE, fd, *r); return *r; }
static inline int raw_stat(const char *n, int *b, int *r) { _SYSCALL4(SYS_STAT, (int)n, (int)b, 0, *r); return *r; }
static inline int raw_sbrk(int sz, int *addr)        { _SYSCALL1(SYS_SBRK, sz, *addr); return *addr; }
static inline int raw_write(const char *s, int len, int *n) { _SYSCALL4(SYS_WRITE, (int)s, len, 0, *n); return *n; }

/* ---- string ---- */
static int strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void *memcpy(void *d, const void *s, int n) {
    char *cd = (char *)d;
    const char *cs = (const char *)s;
    for (int i = 0; i < n; i++) cd[i] = cs[i];
    return d;
}
static void *memset(void *d, int c, int n) {
    char *cd = (char *)d;
    for (int i = 0; i < n; i++) cd[i] = (char)c;
    return d;
}

/* ---- serial output (no format — use explicit calls) ---- */
static void write_str(const char *s) {
    int n, r;
    for (n = 0; s[n]; n++);
    raw_write(s, n, &r);
}
static void write_int(int v) {
    char buf[12]; int i = 0;
    if (v < 0) { write_str("-"); v = -v; }
    if (v == 0) buf[i++] = '0';
    else while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i) { int r; raw_write(&buf[--i], 1, &r); }
}
static void write_hex(int v) {
    write_str("0x");
    for (int sh = 28; sh >= 0; sh -= 4) {
        char c = "0123456789ABCDEF"[(v >> sh) & 0xF];
        int r; raw_write(&c, 1, &r);
    }
}

/* ---- heap (sbrk-based bump allocator) ---- */
#define HEAP_PAGE 4096

typedef struct HeapBlock {
    int size;
    struct HeapBlock *next;
} HeapBlock;

static HeapBlock *heap_start __attribute__((section(".data"))) = 0;

static void *malloc(int size) {
    if (size <= 0) return 0;
    size = (size + 7) & ~7;

    if (!heap_start) {
        int addr;
        raw_sbrk(HEAP_PAGE, &addr);
        if (addr <= 0) return 0;
        heap_start = (HeapBlock *)addr;
        heap_start->size = HEAP_PAGE - (int)sizeof(HeapBlock);
        heap_start->next = 0;
    }

    HeapBlock *prev = 0, *cur = heap_start;
    while (cur) {
        if (cur->size > 0 && cur->size >= size + (int)sizeof(HeapBlock)) {
            if (cur->size >= size + (int)sizeof(HeapBlock) + 16) {
                HeapBlock *nxt = (HeapBlock *)((char *)(cur + 1) + size);
                nxt->size = cur->size - size - (int)sizeof(HeapBlock);
                nxt->next = cur->next;
                cur->size = size;
                cur->next = nxt;
            }
            cur->size = -cur->size;
            return (void *)(cur + 1);
        }
        prev = cur;
        cur = cur->next;
    }

    int need = (size + (int)sizeof(HeapBlock) + HEAP_PAGE - 1) & ~(HEAP_PAGE - 1);
    int addr;
    raw_sbrk(need, &addr);
    if (addr <= 0) return 0;
    HeapBlock *blk = (HeapBlock *)addr;
    blk->size = -size;
    blk->next = 0;
    if (prev) prev->next = blk;
    return (void *)(blk + 1);
}

static void free(void *ptr) {
    if (!ptr) return;
    HeapBlock *blk = (HeapBlock *)ptr - 1;
    blk->size = -blk->size;
    HeapBlock *nxt = blk->next;
    if (nxt && nxt->size > 0) {
        blk->size += nxt->size + (int)sizeof(HeapBlock);
        blk->next = nxt->next;
    }
}

/* ---- file I/O ---- */
typedef struct { int fd; } FILE;

static FILE *fopen(const char *path) {
    int fd;
    raw_open(path, &fd);
    if (fd < 0) return 0;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { int r; raw_close(fd, &r); return 0; }
    f->fd = fd;
    return f;
}

static int fread(void *buf, int size, FILE *f) {
    if (!f || f->fd < 0) return -1;
    int n;
    raw_read(f->fd, (char *)buf, size, &n);
    return n;
}

static void fclose(FILE *f) {
    if (f) { int r; raw_close(f->fd, &r); free(f); }
}

static int file_size(const char *path) {
    int info[4];
    int r;
    raw_stat(path, info, &r);
    if (r < 0) return -1;
    return info[0];
}

#ifdef __cplusplus
}
#endif
