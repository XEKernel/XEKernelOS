#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/types.h"

void paging_init(void) {
    u32 fbaddr = *(u32 *)0x500;
    u32 page_dir = mm_alloc_page();
    u32 *pd = (u32 *)page_dir;

    for (int i = 0; i < 1024; i++) pd[i] = 0;

    for (int i = 0; i < 4; i++)
        pd[i] = (i * 0x400000) | 0x87;

    if (fbaddr >= 0x100000) {
        int idx = fbaddr / 0x400000;
        pd[idx] = (idx * 0x400000) | 0x87;
    }

    u32 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("mov %0, %%cr3" : : "r"(page_dir));

    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;

    __asm__ volatile(
        "mov %0, %%cr0\n"
        "jmp 1f\n"
        "1:\n"
        :
        : "r"(cr0)
    );
}
