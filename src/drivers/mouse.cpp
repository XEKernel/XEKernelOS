#include "drivers/mouse.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "kernel/isr.h"
#include "lib/ports.h"

Mouse mouse;

/* Timeout-protected PS/2 helpers */
static int wready() {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return 1;
    return 0;
}
static int rready() {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return 1;
    return 0;
}
static void mwrite(u8 d) { if (wready()) { outb(0x64, 0xD4); if (wready()) outb(0x60, d); } }

void Mouse::irq_handler() {
    static int cnt = 0;
    u8 st = inb(0x64);
    if (st & 0x20) {
        if (cnt < 3) { serial_write_char('M'); cnt++; }
        u8 data = inb(0x60);
        mouse.pkt_[mouse.cycle_] = data;
        mouse.cycle_ = (mouse.cycle_ + 1) % 3;
        if (mouse.cycle_ == 0) {
            u8 b = mouse.pkt_[0];
            int dx = (b & 0x10) ? (int)(mouse.pkt_[1] | 0xFFFFFF00) : mouse.pkt_[1];
            int dy = (b & 0x20) ? (int)(mouse.pkt_[2] | 0xFFFFFF00) : mouse.pkt_[2];
            mouse.mx_ += dx; mouse.my_ -= dy;
            if (mouse.mx_ < 0) mouse.mx_ = 0;
            if (mouse.my_ < 0) mouse.my_ = 0;
            if (mouse.mx_ > 1023) mouse.mx_ = 1023;
            if (mouse.my_ > 767) mouse.my_ = 767;
            mouse.mbtn_ = b & 7;
        }
    }
}

void Mouse::init() {
    serial_write_str("mouse: probing...\n");

    if (!wready()) { serial_write_str("mouse: no controller\n"); return; }
    outb(0x64, 0xA8);               /* enable aux port */
    if (!wready()) { serial_write_str("mouse: aux fail\n"); return; }
    outb(0x64, 0x20);               /* read config */
    if (!rready()) { serial_write_str("mouse: config read fail\n"); return; }
    u8 cfg = inb(0x60);
    cfg |= 2;                       /* enable IRQ12 */
    if (!wready()) return;
    outb(0x64, 0x60);               /* write config */
    if (!wready()) return;
    outb(0x60, cfg);

    mwrite(0xFF);                   /* reset */
    if (!rready()) { serial_write_str("mouse: no ack to reset\n"); return; }
    inb(0x60);                      /* ack */
    if (!rready()) return;
    inb(0x60);                      /* self-test 0xAA */
    if (!rready()) return;
    inb(0x60);                      /* device ID 0x00 */

    mwrite(0xF4);                   /* enable reporting */
    if (!rready()) { serial_write_str("mouse: no ack to enable\n"); return; }
    inb(0x60);

    cycle_ = 0;
    isr_register(0x2C, irq_handler);
    pic_unmask_irq(12);
    serial_write_str("mouse: init ok\n");
}

int Mouse::get(int *x, int *y, int *btn) {
    *x = mx_; *y = my_; *btn = mbtn_;
    return 1;
}
