#include "kernel/isr.h"
#include "kernel/panic.h"
#include "kernel/task.h"
#include "kernel/paging.h"
#include "kernel/user.h"
#include "kernel/syscall.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "shell/shell.h"
#include "lib/ports.h"

IsrManager isr_mgr;

void IsrManager::register_handler(int vec, void (*fn)(void)) {
    if (vec >= 0 && vec < 256) handlers_[vec] = fn;
}

extern "C" void syscall_handler(registers_t *r);

extern "C" void c_isr_handler(registers_t *r) {
    int vec = r->vec;

    /* Identify interrupt source */
    if (vec == 0x80)
        __asm__ volatile("movb $'S', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    else if (vec == 0x20)
        __asm__ volatile("movb $'T', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    else {
        static const char hx[] = "0123456789ABCDEF";
        char hi = hx[(vec >> 4) & 0xF];
        char lo = hx[vec & 0xF];
        __asm__ volatile("movb $'!', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
        __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(hi) : "dx","al");
        __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(lo) : "dx","al");
    }

    /* CR3 protection: if came from user mode, switch to kernel page table */
    int from_user = ((r->cs & 3) == 3);
    if (from_user) {
        PagingManager::get_kernel_paging()->load();
    }

    void (*h)(void) = isr_mgr.lookup(vec);
    if (h) h();

    if (vec == 0x80) {
        syscall_handler(r);
        /* fall through to CR3 restore at end */
    }

    if (vec == 0x20) {
        if (kb_ctrl_c()) {
            shell_recover(r);
            return;
        }
        schedule(r);
        return;
    }

    if (vec == 14) {
        u32 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        gfx_puts("\n#PF at EIP=0x");
        gfx_put_hex_u32(r->eip);
        gfx_puts(" fault_addr=0x");
        gfx_put_hex_u32(cr2);
        gfx_puts(" err=");
        gfx_put_hex_u32(r->err_code);
        if (r->err_code & 1) gfx_puts(" present");
        else gfx_puts(" not-present");
        if (r->err_code & 2) gfx_puts(" write");
        else gfx_puts(" read");
        if (r->err_code & 4) gfx_puts(" user");
        else gfx_puts(" supervisor");
        if (r->err_code & 8) gfx_puts(" reserved");
        gfx_putc('\n');
        kernel_panic(r, "Page Fault");
    }

    if (vec >= 0x20 && vec <= 0x2F) {
        outb(0x20, 0x20);
        if (vec >= 0x28) outb(0xA0, 0x20);
    }

    if (vec < 0x20) {
        static const char *names[] = {
            "#DE","#DB","NMI","#BP","#OF","#BR","#UD","#NM",
            "#DF","","#TS","#NP","#SS","#GP","#PF","",
            "#MF","#AC","#MC","#XM","#VE"
        };
        const char *n = (vec <= 20 && names[vec][0]) ? names[vec] : "?";
        kernel_panic(r, n);
    }

    /* Restore user page directory before returning to ring 3 */
    if (from_user && g_user_pd) {
        outb(0xA1, 0xFF);          /* mask all slave PIC IRQs */
        g_user_pd->load();
    }
}
