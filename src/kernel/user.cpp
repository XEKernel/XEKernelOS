#include "kernel/user.h"
#include "kernel/mm.h"
#include "lib/types.h"

static u32 tss_page;

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;
    for (int i = 0; i < 104; i++) tss[i] = 0;
    *(u32 *)(tss + 4) = 0x9F000;
    *(u32 *)(tss + 8) = 0x10;

    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));

    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;
    gdt[idx*8+2] = tss_page & 0xFF;
    gdt[idx*8+3] = (tss_page >> 8) & 0xFF;
    gdt[idx*8+4] = (tss_page >> 16) & 0xFF;
    gdt[idx*8+7] = (tss_page >> 24) & 0xFF;

    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void user_run(void (*entry)(void)) {
    __asm__ volatile(
        "pushl $0x23\n"
        "pushl $0x9E000\n"
        "pushf\n"
        "orl $0x200, (%%esp)\n"
        "pushl $0x2B\n"
        "pushl %0\n"
        "iretl\n"
        : : "r"(entry)
    );
}
