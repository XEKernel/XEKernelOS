/* user.cpp — user mode: TSS setup + enter_user_mode() */
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/ports.h"

static u32 tss_page;
PagingManager *g_user_pd = nullptr;

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;

    /* Zero the TSS */
    for (int i = 0; i < 104; i++) tss[i] = 0;

    /* ESP0 = kernel stack for ring3→ring0 transitions */
    *(u32 *)(tss + 4)  = 0x9F000;  /* ESP0 */
    *(u32 *)(tss + 8)  = 0x10;     /* SS0 */

    /* I/O permission bitmap: allow ports 0x3F8-0x3FF (serial) for ring3.
       Bitmap byte N covers ports N*8 to N*8+7.
       Port 0x3F8 = 1016 → byte 127 (1016/8).
       Byte 127 bits: ports 1016-1023 → all 0 (allow) for serial range.
       Port 0x3FD = 1021 → also in byte 127.
       Terminate bitmap with 0xFF byte at position 128. */
    tss[102] = 104;  /* I/O Map Base = offset 104 (right after TSS base) */
    tss[103] = 0;

    /* Bitmap follows at offset 104: */
    u8 *iobm = tss + 104;
    for (int i = 0; i < 128; i++) iobm[i] = 0x00;  /* all ports allowed */
    iobm[127] = 0x00;                    /* ports 1016-1023 (COM1) allowed */
    iobm[128] = 0xFF;                    /* terminator */

    /* Install TSS descriptor in GDT at slot 6 (selector 0x30) */
    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));

    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;
    u32 base = tss_page;
    gdt[idx*8 + 2] = base & 0xFF;
    gdt[idx*8 + 3] = (base >> 8) & 0xFF;
    gdt[idx*8 + 4] = (base >> 16) & 0xFF;
    gdt[idx*8 + 7] = (base >> 24) & 0xFF;

    /* Update TSS limit in GDT: base size (104) + I/O bitmap (129 bytes) */
    u16 new_limit = 104 + 129 - 1;
    gdt[idx*8 + 0] = new_limit & 0xFF;
    gdt[idx*8 + 1] = (new_limit >> 8) & 0xFF;

    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

/* Single entry point: load user page table, build iret frame, switch to ring3.
   The user code will run at `entry` with CPL=3, interrupts disabled. */
void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd) {
    if (!pd) return;

    g_user_pd = pd;

    /* Use kernel stack at 0x9F000 for the iret frame.
       After iret, ESP switches to user stack. */
    u32 *sp = (u32 *)0x9F000;

    /* Build iret frame for cross-privilege return to ring 3 */
    *(--sp) = 0x23;        /* SS = user data */
    *(--sp) = stack_top;   /* ESP = user stack top */
    *(--sp) = 0x002;       /* EFLAGS (IF=0) */
    *(--sp) = 0x2B;        /* CS = user code, RPL=3 */
    *(--sp) = entry;       /* EIP = user code entry */

    /* Load user page directory */
    __asm__ volatile("movb $'P', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    {
        u32 p = pd->get_page_dir_phys();
        for (int s = 28; s >= 0; s -= 4) {
            char c = "0123456789ABCDEF"[(p >> s) & 15];
            __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(c) : "dx","al");
        }
    }
    pd->load();
    /* Extra full TLB flush — belt and suspenders */
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");

    /* Verify by reading the physical page directly via identity mapping.
       Get physical address from the user page directory's PDE/PTE. */
    {
        u32 *user_pd_virt = (u32 *)pd->get_page_dir_phys();
        u32 pde = user_pd_virt[32];  /* 0x08000000 >> 22 = 32 */
        /* Output PDE as 8 hex digits */
        __asm__ volatile("movb $'R', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
        for (int s = 28; s >= 0; s -= 4) {
            char c = "0123456789ABCDEF"[(pde >> s) & 15];
            __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(c) : "dx","al");
        }
        if (!(pde & 1)) {
            __asm__ volatile("movb $'N', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
        } else if (pde & PAGE_PSE) {
            __asm__ volatile("movb $'B', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
        } else {
            u32 *pt = (u32 *)(pde & 0xFFFFF000);
            u32 pte = pt[0];
            __asm__ volatile("movb $'T', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
            for (int s = 28; s >= 0; s -= 4) {
                char c = "0123456789ABCDEF"[(pte >> s) & 15];
                __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(c) : "dx","al");
            }
            u8 *code = (u8 *)(pte & 0xFFFFF000);
            u8 b = code[0];
            char lo = "0123456789ABCDEF"[b & 15];
            __asm__ volatile("movb $'X', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
            __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(lo) : "dx","al");
        }
    }

    /* Force read via fixed register — no compiler alias shenanigans */
    volatile u8 v;
    __asm__ volatile("movb (%%eax), %%al" : "=a"(v) : "a"(entry) : "memory");
    __asm__ volatile("movb $'V', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    {
        char lo = "0123456789ABCDEF"[v & 15];
        __asm__ volatile("movb %0, %%al; movw $0x3F8, %%dx; outb %%al, %%dx" :: "r"(lo) : "dx","al");
    }

    /* Debug: confirm we're about to iret */
    __asm__ volatile("movb $'E', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    /* Switch to frame and iret to ring 3 */
    __asm__ volatile(
        "mov %0, %%esp\n"
        "iret\n"
        :
        : "r"(sp)
    );
    __builtin_unreachable();
}
