#include "kernel/syscall.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "fs/fat12.h"
#include "lib/heap.h"
#include "lib/ports.h"

/* File descriptor: single-open for now */
static u8  *f_buf = nullptr;
static u32  f_size = 0;

/* Heap break — starts at 0x10000000 (PDE 64, clear of kernel PDEs) */
static u32 user_break = 0x10000000;

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    for (u32 i = 0; i < len; i++) {
        for (volatile int t = 0; t < 10000000; t++)
            if (inb(0x3FD) & 0x20) break;
        outb(0x3F8, (u8)str[i]);
    }
    for (volatile int t = 0; t < 10000000; t++)
        if (inb(0x3FD) & 0x20) break;
    outb(0x3F8, '\n');
    r->eax = len;
}

static void sys_read(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (max <= 0 || !buf) { r->eax = 0; return; }
    kb_readline(buf, max - 1);
    buf[max - 1] = 0;
    int n = 0;
    while (buf[n]) n++;
    r->eax = n;
}

static void sys_open(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }

    /* Free previous file if any */
    if (f_buf) { kfree(f_buf); f_buf = nullptr; f_size = 0; }

    /* Allocate buffer and read file */
    f_buf = (u8 *)kmalloc(65536);
    if (!f_buf) { r->eax = (u32)-1; return; }

    int sz = fat_read_file_buf(name, f_buf, 65536);
    if (sz <= 0) { kfree(f_buf); f_buf = nullptr; r->eax = (u32)-1; return; }

    f_size = (u32)sz;
    r->eax = 0;  /* fd 0 */
}

static void sys_fread(registers_t *r) {
    u32 fd = r->ebx;
    char *buf = (char *)r->ecx;
    u32 len = r->edx;

    if (fd != 0 || !f_buf || !buf) { r->eax = (u32)-1; return; }
    if (len > f_size) len = f_size;
    for (u32 i = 0; i < len; i++) buf[i] = f_buf[i];
    r->eax = len;
}

static void sys_sbrk(registers_t *r) {
    u32 bytes = r->ebx;
    if (bytes == 0) { r->eax = user_break; return; }

    /* Page-align the request upward */
    u32 pages = (bytes + 0xFFF) / 0x1000;
    u32 old_break = user_break;

    if (!g_user_pd) { r->eax = (u32)-1; return; }

    for (u32 i = 0; i < pages; i++) {
        u32 phys = mm_alloc_page();
        if (!phys) { r->eax = (u32)-1; return; }
        /* Map into user page directory */
        g_user_pd->map_page(user_break, phys, PT_FLAGS);
        user_break += 0x1000;
    }

    r->eax = old_break;
}

static void sys_getcwd(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (!buf || max <= 0) { r->eax = (u32)-1; return; }
    fat.cwd_str(buf, max);
    int n = 0; while (buf[n]) n++;
    r->eax = n;
}

static void sys_time(registers_t *r) {
    char *buf = (char *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    /* Read CMOS RTC: seconds at 0x00, minutes at 0x02, hours at 0x04 */
    auto bcd = [](u8 v) -> u8 { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); };
    outb(0x70, 0x04); u8 h = bcd(inb(0x71));
    outb(0x70, 0x02); u8 m = bcd(inb(0x71));
    outb(0x70, 0x00); u8 s = bcd(inb(0x71));
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10); buf[5] = ':';
    buf[6] = '0' + (s / 10); buf[7] = '0' + (s % 10); buf[8] = 0;
    r->eax = 8;
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
    case SYS_EXIT:
        PagingManager::get_kernel_paging()->load();
        if (f_buf) { kfree(f_buf); f_buf = nullptr; f_size = 0; }
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
