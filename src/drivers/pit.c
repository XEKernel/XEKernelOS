#include "drivers/pit.h"
#include "drivers/pic.h"
#include "kernel/isr.h"
#include "lib/ports.h"

#define PIT_CH0     0x40
#define PIT_CMD     0x43

static volatile u32 ticks;

static void pit_handler(void) {
    ticks++;
}

void pit_init(void) {
    u32 div = 1193180 / 100;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, div & 0xFF);
    outb(PIT_CH0, (div >> 8) & 0xFF);
    ticks = 0;
    isr_register(0x20, pit_handler);
}

void pit_sleep(int ms) {
    u32 target = ticks + (ms / 10);
    while (ticks < target) __asm__ volatile("hlt");
}
