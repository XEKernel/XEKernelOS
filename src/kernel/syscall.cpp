#include "kernel/syscall.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "lib/ports.h"

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    for (u32 i = 0; i < len; i++) {
        for (volatile int t = 0; t < 1000000; t++)
            if (inb(0x3FD) & 0x20) break;
        outb(0x3F8, (u8)str[i]);
    }
    for (volatile int t = 0; t < 1000000; t++)
        if (inb(0x3FD) & 0x20) break;
    outb(0x3F8, '\n');
    r->eax = len;
}

extern "C" void syscall_handler(registers_t *r) {
    __asm__ volatile("movb $'[', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    char c = '0' + (r->eax % 10);
    __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"((char)c) : "dx","al");
    __asm__ volatile("movb $']', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    switch (r->eax) {
    case SYS_WRITE:
        sys_write(r);
        break;
    case SYS_EXIT:
        /* Restore kernel CR3 (in case user had its own page table) */
        PagingManager::get_kernel_paging()->load();
        /* Restore kernel ESP to pre-enter_user_mode state.
           The 'ret' unwinds: load_elf → load_binary → cmd_run → shell_loop */
        __asm__ volatile(
            "mov %0, %%esp\n"
            "xor %%eax, %%eax\n"
            "ret\n"
            :
            : "m"(g_entry_esp)
        );
        __builtin_unreachable();
    default:
        r->eax = -1;
        break;
    }
}
