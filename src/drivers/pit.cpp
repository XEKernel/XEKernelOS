#include "drivers/pit.h"
#include "drivers/pic.h"
#include "drivers/gfx.h"
#include "kernel/isr.h"
#include "lib/ports.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43

volatile u32 PitTimer::ticks_ = 0;
PitTimer pit;

void PitTimer::handler() {
    ticks_++;
    gfx.mcursor_update();  /* every tick ≈ 100Hz for smooth cursor */
}

void PitTimer::init() {
    u32 div = 1193180 / 100;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, div & 0xFF);
    outb(PIT_CH0, (div >> 8) & 0xFF);
    ticks_ = 0;
    isr_register(0x20, handler);
    pic_unmask_irq(0);  /* PIT IRQ0 was masked in PIC initial config */
}

void PitTimer::sleep(int ms) {
    u32 target = ticks_ + (ms / 10);
    while (ticks_ < target) __asm__ volatile("hlt");
}
