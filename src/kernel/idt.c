#include "kernel/idt.h"

extern u32 isr_addrs[];
extern void isr128(void);

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 offset_high;
} __attribute__((packed));

static struct idt_entry idt[256];

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        u32 addr = (i < 48) ? isr_addrs[i] : 0;
        idt[i].offset_low  = addr & 0xFFFF;
        idt[i].selector    = 0x18;
        idt[i].zero        = 0;
        idt[i].flags       = 0x8E;
        idt[i].offset_high = (addr >> 16) & 0xFFFF;
    }
    {
        u32 addr = (u32)isr128;
        idt[0x80].offset_low  = addr & 0xFFFF;
        idt[0x80].selector    = 0x18;
        idt[0x80].zero        = 0;
        idt[0x80].flags       = 0xEE;
        idt[0x80].offset_high = (addr >> 16) & 0xFFFF;
    }
    struct { u16 limit; u32 base; } __attribute__((packed)) idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u32)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
