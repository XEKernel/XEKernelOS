#include "kernel/isr.h"
#include "kernel/panic.h"
#include "kernel/task.h"
#include "kernel/paging.h"
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

    /* CR3 exit: restore user page table if returning to user mode.
       If schedule() was called, it already loaded the correct CR3 via the
       new task. If not, we need to restore the user's CR3 here. */
    if (from_user && current_task && current_task->paging) {
        /* Only reload if schedule didn't already do it.
           We reload unconditionally — it's fast and ensures correctness. */
        current_task->paging->load();
    }
}
