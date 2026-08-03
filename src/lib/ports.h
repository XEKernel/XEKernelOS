#pragma once
#include "lib/types.h"

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %w1" : : "a"(val), "Nd"(port));
}

static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %w1" : : "a"(val), "Nd"(port));
}
