#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/ports.h"

static u32 tss_page;
PagingManager *g_user_pd = nullptr;
u32 g_entry_esp = 0;
char g_user_args[256];

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;
    for (int i = 0; i < 104; i++) tss[i] = 0;
    *(u32 *)(tss + 4)  = 0x9F000;
    *(u32 *)(tss + 8)  = 0x10;

    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;
    u32 base = tss_page;
    gdt[idx*8 + 2] = base & 0xFF;
    gdt[idx*8 + 3] = (base >> 8) & 0xFF;
    gdt[idx*8 + 4] = (base >> 16) & 0xFF;
    gdt[idx*8 + 7] = (base >> 24) & 0xFF;
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd,
                     int argc, const char *args) {
    (void)stack_top;

    __asm__ volatile("mov %%ebp, %0" : "=m"(g_entry_esp));

    if (args && args[0]) {
        /* Copy args into global buffer (accessible from user CR3) */
        int i = 0;
        while (args[i] && i < 255) { g_user_args[i] = args[i]; i++; }
        g_user_args[i] = 0;
    } else {
        g_user_args[0] = 0;
    }

    if (pd) {
        g_user_pd = pd;
        pd->load();
    }

    if (argc > 0 && g_user_args[0]) {
        /* Push argv pointers + argc onto user stack (at 0x9E000).
           The iret sets ESP=0x9E000, so args sit just below it.
           Layout: [ESP] = argc, [ESP+4] = argv[0], ... [ESP+4*argc] = 0 */
        char *argp = g_user_args;
        u32 *stk = (u32 *)0x9E000;

        /* Push null sentinel + argv pointers */
        *(--stk) = 0;  /* null terminator */
        int ac = 0;
        for (ac = 0; ac < argc; ac++) {
            *(--stk) = (u32)argp;  /* argv[ac] */
            while (*argp && *argp != ' ') argp++;
            if (*argp == ' ') { *argp++ = 0; }
            while (*argp == ' ') argp++;
            if (*argp == 0) break;
        }
        *(--stk) = ac + 1;  /* argc */

        __asm__ volatile(
            "pushl %0\n"   /* SS */
            "pushl %1\n"   /* ESP (argc location) */
            "pushf\n"
            "orl $0x200, (%%esp)\n"
            "pushl $0x2B\n"
            "pushl %2\n"   /* EIP */
            "iret\n"
            :
            : "i"(0x23), "r"((u32)(stk)), "r"(entry)
        );
    } else {
        __asm__ volatile(
            "pushl $0x23\n"
            "pushl $0x9E000\n"
            "pushf\n"
            "orl $0x200, (%%esp)\n"
            "pushl $0x2B\n"
            "pushl %0\n"
            "iret\n"
            :
            : "r"(entry)
        );
    }
    __builtin_unreachable();
}
