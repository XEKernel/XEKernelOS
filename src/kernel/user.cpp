#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/ports.h"

static u32 tss_page;
PagingManager *g_user_pd = nullptr;
u32 g_entry_esp = 0;

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;
    for (int i = 0; i < 104; i++) tss[i] = 0;
    *(u32 *)(tss + 4)  = 0x9F000;
    *(u32 *)(tss + 8)  = 0x10;

    /* Update GDT TSS descriptor base to point to our allocated TSS */
    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;  /* TSS selector 0x30 = index 6 */
    u32 base = tss_page;
    gdt[idx*8 + 2] = base & 0xFF;
    gdt[idx*8 + 3] = (base >> 8) & 0xFF;
    gdt[idx*8 + 4] = (base >> 16) & 0xFF;
    gdt[idx*8 + 7] = (base >> 24) & 0xFF;

    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd) {
    (void)pd;
    (void)stack_top;

    /* Save EBP so SYS_EXIT can find the return address (at EBP+4) */
    __asm__ volatile("mov %%ebp, %0" : "=m"(g_entry_esp));

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
