#include "kernel/syscall.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "fs/fat12.h"
#include "lib/heap.h"
#include "lib/ports.h"

/* File descriptor: single-open for now */
static u8  *f_buf = nullptr;
static u32  f_size = 0;

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
