#include "kernel/idt.h"

extern u32 isr_addrs[];

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 offset_high;
} __attribute__((packed));

static struct idt_entry idt[48];

void idt_init(void) {
    for (int i = 0; i < 48; i++) {
        u32 addr = isr_addrs[i];
        idt[i].offset_low  = addr & 0xFFFF;
        idt[i].selector    = 0x18;
        idt[i].zero        = 0;
        idt[i].flags       = 0x8E;
        idt[i].offset_high = (addr >> 16) & 0xFFFF;
    }
    struct { u16 limit; u32 base; } __attribute__((packed)) idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u32)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
