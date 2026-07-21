#include "drivers/mouse.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "kernel/isr.h"
#include "lib/ports.h"

Mouse mouse;

void Mouse::wait(u8 write_mode) {
    for (int i = 0; i < 100000; i++) {
        u8 st = inb(0x64);
        if (write_mode) {
            if (!(st & 2)) return;
        } else {
            if (st & 1) return;
        }
    }
}

void Mouse::write(u8 data) {
    wait(1);
    outb(0x64, 0xD4);
    wait(1);
    outb(0x60, data);
}

u8 Mouse::read() {
    wait(0);
    return inb(0x60);
}

void Mouse::irq_handler() {
    static int mcount = 0;
    u8 st = inb(0x64);
    if ((st & 0x20) && (st & 1)) {
        mcount++;
        if (mcount <= 3) serial_write_char('M');
        u8 data = inb(0x60);
        mouse.pkt_[mouse.cycle_] = data;
        mouse.cycle_ = (mouse.cycle_ + 1) % 3;
        if (mouse.cycle_ == 0) {
            u8 b = mouse.pkt_[0];
            int dx = (b & 0x10) ? (mouse.pkt_[1] | 0xFFFFFF00) : mouse.pkt_[1];
            int dy = (b & 0x20) ? (mouse.pkt_[2] | 0xFFFFFF00) : mouse.pkt_[2];
            mouse.mx_ += dx; mouse.my_ -= dy;
            mouse.mbtn_ = b & 7;
        }
    }
}

void Mouse::init() {
    wait(1); outb(0x64, 0xA8);
    wait(1); outb(0x64, 0x20);
    u8 cmd = read();
    wait(1); outb(0x64, 0x60);
    wait(1); outb(0x60, cmd | 2);

    write(0xF6); read();   /* set defaults */
    write(0xF4); read();   /* enable data reporting — THIS WAS MISSING! */

    cycle_ = 0;
    isr_register(0x2C, irq_handler);
    pic_unmask_irq(12);
}

int Mouse::get(int *x, int *y, int *btn) {
    *x = mx_; *y = my_; *btn = mbtn_;
    return 1;
}
