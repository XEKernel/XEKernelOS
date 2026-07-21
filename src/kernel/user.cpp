#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/ports.h"

static u32 tss_page;
PagingManager *g_user_pd = nullptr;

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;
    for (int i = 0; i < 104; i++) tss[i] = 0;
    *(u32 *)(tss + 4)  = 0x9F000;
    *(u32 *)(tss + 8)  = 0x10;

    /* NO I/O bitmap modification — keep original TSS limit */
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd) {
    (void)pd;  /* not used — keep kernel CR3 */
    (void)stack_top;  /* not used — use kernel stack area */

    /* Original user_run-style: push iret frame, iret to ring 3.
       Uses kernel CR3 — kernel PDEs now include PAGE_USER (0x87). */
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
    __builtin_unreachable();
}
