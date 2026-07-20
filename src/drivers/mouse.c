#include "drivers/mouse.h"
#include "kernel/isr.h"
#include "lib/ports.h"

static volatile int mx, my, mbtn;

static void mouse_wait(u8 write_mode) {
    for (int i = 0; i < 100000; i++) {
        u8 st = inb(0x64);
        if (write_mode) {
            if (!(st & 2)) return;
        } else {
            if (st & 1) return;
        }
    }
}

static void mouse_write(u8 data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

static u8 mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static int mouse_cycle;
static u8 mouse_pkt[3];

static void mouse_irq(void) {
    u8 st = inb(0x64);
    if ((st & 0x20) && (st & 1)) {
        u8 data = inb(0x60);
        mouse_pkt[mouse_cycle] = data;
        mouse_cycle = (mouse_cycle + 1) % 3;
        if (mouse_cycle == 0) {
            u8 b = mouse_pkt[0];
            int dx = (b & 0x10) ? (mouse_pkt[1] | 0xFFFFFF00) : mouse_pkt[1];
            int dy = (b & 0x20) ? (mouse_pkt[2] | 0xFFFFFF00) : mouse_pkt[2];
            mx += dx; my -= dy;
            mbtn = b & 7;
        }
    }
}

void mouse_init(void) {
    mouse_wait(1); outb(0x64, 0xA8);
    mouse_wait(1); outb(0x64, 0x20);
    u8 cmd = mouse_read();
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, cmd | 2);

    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();

    mouse_cycle = 0;
    isr_register(0x2C, mouse_irq);
    outb(0xA1, inb(0xA1) & ~0x10);
}

int mouse_get(int *x, int *y, int *btn) {
    *x = mx; *y = my; *btn = mbtn;
    return 1;
}
