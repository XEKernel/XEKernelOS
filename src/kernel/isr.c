#include "kernel/isr.h"
#include "kernel/panic.h"
#include "drivers/gfx.h"
#include "lib/ports.h"

static void (*handlers[256])(void);

void isr_register(int vec, void (*fn)(void)) {
    if (vec >= 0 && vec < 256) handlers[vec] = fn;
}

void syscall_handler(registers_t *r);

void c_isr_handler(registers_t *r) {
    int vec = r->vec;

    if (handlers[vec]) handlers[vec]();

    if (vec == 0x80) {
        syscall_handler(r);
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
}
