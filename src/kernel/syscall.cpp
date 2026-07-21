#include "kernel/syscall.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/gfx.h"
#include "fs/fat12.h"
#include "lib/heap.h"
#include "lib/ports.h"

#define MAX_FD 4

/* File descriptor table */
static u8  *fd_buf[MAX_FD] = {nullptr};
static u32  fd_size[MAX_FD] = {0};

/* Heap break — starts at 0x10000000 (PDE 64, clear of kernel PDEs) */
static u32 user_break = 0x10000000;

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    if (!str || len > 4096) { r->eax = (u32)-1; return; }
    serial_write_str_len(str, len);
    serial_write_char('\n');
    r->eax = len;
}

static void sys_read(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (max <= 0 || max > 4096 || !buf) { r->eax = 0; return; }
    kb_readline(buf, max - 1);
    buf[max - 1] = 0;
    int n = 0; while (buf[n]) n++;
    r->eax = n;
}

static void sys_open(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }

    /* Find free fd slot */
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_buf[i]) { fd = i; break; }
    }
    if (fd < 0) { r->eax = (u32)-1; return; }

    u8 *fb = (u8 *)kmalloc(65536);
    if (!fb) { r->eax = (u32)-1; return; }

    int sz = fat_read_file_buf(name, fb, 65536);
    if (sz <= 0) { kfree(fb); r->eax = (u32)-1; return; }

    fd_buf[fd] = fb;
    fd_size[fd] = (u32)sz;
    r->eax = fd;
}

static void sys_fread(registers_t *r) {
    u32 fd = r->ebx;
    char *buf = (char *)r->ecx;
    u32 len = r->edx;

    if (fd >= MAX_FD || !fd_buf[fd] || !buf) { r->eax = (u32)-1; return; }
    if (len > fd_size[fd]) len = fd_size[fd];
    if (len > 4096) len = 4096;
    for (u32 i = 0; i < len; i++) buf[i] = fd_buf[fd][i];
    r->eax = len;
}

static void sys_sbrk(registers_t *r) {
    u32 bytes = r->ebx;
    if (bytes == 0) { r->eax = user_break; return; }

    u32 pages = (bytes + 0xFFF) / 0x1000;
    u32 old_break = user_break;

    if (!g_user_pd) { r->eax = (u32)-1; return; }

    for (u32 i = 0; i < pages; i++) {
        u32 phys = mm_alloc_page();
        if (!phys) { r->eax = (u32)-1; return; }
        g_user_pd->map_page(user_break, phys, PT_FLAGS);
        user_break += 0x1000;
    }

    r->eax = old_break;
}

static void sys_getcwd(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (!buf || max <= 0 || max > 256) { r->eax = (u32)-1; return; }
    fat.cwd_str(buf, max);
    int n = 0; while (buf[n]) n++;
    r->eax = n;
}

static void sys_time(registers_t *r) {
    char *buf = (char *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    auto bcd = [](u8 v) -> u8 { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); };
    outb(0x70, 0x04); u8 h = bcd(inb(0x71));
    outb(0x70, 0x02); u8 m = bcd(inb(0x71));
    outb(0x70, 0x00); u8 s = bcd(inb(0x71));
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10); buf[5] = ':';
    buf[6] = '0' + (s / 10); buf[7] = '0' + (s % 10); buf[8] = 0;
    r->eax = 8;
}

static void sys_close(registers_t *r) {
    u32 fd = r->ebx;
    if (fd >= MAX_FD || !fd_buf[fd]) { r->eax = (u32)-1; return; }
    kfree(fd_buf[fd]);
    fd_buf[fd] = nullptr;
    fd_size[fd] = 0;
    r->eax = 0;
}

static void sys_getfb(registers_t *r) {
    /* Fill user buffer with {fb_addr, w, h, pitch, bpp} (5 × u32) */
    u32 *buf = (u32 *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    buf[0] = (u32)gfx.fb_addr();
    buf[1] = (u32)gfx.fb_width();
    buf[2] = (u32)gfx.fb_height();
    buf[3] = (u32)gfx.fb_pitch();
    buf[4] = (u32)gfx.fb_bpp();
    r->eax = 5;  /* number of u32 values written */
}

extern "C" void syscall_handler(registers_t *r) {
    __asm__ volatile("movb $'[', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    char c = '0' + (r->eax % 10);
    __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"((char)c) : "dx","al");
    __asm__ volatile("movb $']', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    switch (r->eax) {
    case SYS_WRITE: sys_write(r); break;
    case SYS_READ:  sys_read(r);  break;
    case SYS_OPEN:  sys_open(r);  break;
    case SYS_FREAD: sys_fread(r); break;
    case SYS_SBRK:  sys_sbrk(r);  break;
    case SYS_GETCWD: sys_getcwd(r); break;
    case SYS_TIME:  sys_time(r);  break;
    case SYS_GETFB: sys_getfb(r); break;
    case SYS_CLOSE: sys_close(r); break;
    case SYS_EXIT:
        PagingManager::get_kernel_paging()->load();
        for (int i = 0; i < MAX_FD; i++) {
            if (fd_buf[i]) { kfree(fd_buf[i]); fd_buf[i] = nullptr; fd_size[i] = 0; }
        }
        __asm__ volatile(
            "mov %0, %%esp\n"
            "pop %%ebp\n"
            "ret\n"
            :
            : "m"(g_entry_esp)
        );
        __builtin_unreachable();
    default:
        r->eax = (u32)-1;
        break;
    }
}
