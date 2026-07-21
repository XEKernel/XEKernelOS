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

    tss[102] = 104; tss[103] = 0;
    u8 *iobm = tss + 104;
    for (int i = 0; i < 128; i++) iobm[i] = 0x00;
    iobm[128] = 0xFF;

    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;
    u32 base = tss_page;
    gdt[idx*8 + 2] = base & 0xFF;
    gdt[idx*8 + 3] = (base >> 8) & 0xFF;
    gdt[idx*8 + 4] = (base >> 16) & 0xFF;
    gdt[idx*8 + 7] = (base >> 24) & 0xFF;
    u16 new_limit = 104 + 129 - 1;
    gdt[idx*8 + 0] = new_limit & 0xFF;
    gdt[idx*8 + 1] = (new_limit >> 8) & 0xFF;
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd) {
    __asm__ volatile("movb $'E', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    if (!pd) { __asm__ volatile("movb $'N', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al"); return; }
    g_user_pd = pd;

    u32 *kern_pd = (u32 *)PagingManager::get_kernel_paging()->get_page_dir_phys();
    __asm__ volatile("movb $'A', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    kern_pd[entry >> 22] = 0x01000087;
    __asm__ volatile("movb $'B', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    u32 *sp = (u32 *)0x9F000;
    *(--sp) = 0x23;
    *(--sp) = stack_top;
    *(--sp) = 0x002;
    *(--sp) = 0x2B;
    *(--sp) = entry;
    __asm__ volatile("movb $'C', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    __asm__ volatile(
        "mov %0, %%esp\n"
        "iret\n"
        :
        : "r"(sp)
    );
    __builtin_unreachable();
}
