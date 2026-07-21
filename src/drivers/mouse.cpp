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
    u8 st = inb(0x64);
    if (st & 0x20) {
        u8 data = inb(0x60);
        mouse.feed_byte(data);
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

    /* Standard init sequence (OSDev recommended) */
    mwrite(0xF6);                   /* set defaults */
    if (rready()) inb(0x60);

    mwrite(0xE8); mwrite(0x02);    /* set resolution: 4 counts/mm */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);

    mwrite(0xE6);                   /* set scaling 1:1 */
    if (rready()) inb(0x60);

    mwrite(0xF3); mwrite(100);     /* set sample rate: 100 Hz */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);

    mwrite(0xF4);                   /* enable reporting */
    if (!rready()) { serial_write_str("mouse: no ack to enable\n"); return; }
    inb(0x60);

    cycle_ = 0;
    /* Don't enable IRQ12 — keyboard polling loop is the sole PS/2 reader.
     * Having two readers (IRQ + polling) causes byte interleaving and
     * permanent packet corruption. */
    serial_write_str("mouse: init ok (polling)\n");
}

int Mouse::get(int *x, int *y, int *btn) {
    /* Data ingestion is handled by:
     *   - keyboard polling loop (read_scan → feed_byte)
     *   - IRQ12 handler (irq_handler → feed_byte)
     * Don't poll here — avoid racing with those paths. */
    *x = mx_; *y = my_; *btn = mbtn_;
    return 1;
}

void Mouse::feed_byte(u8 data) {
    /* Byte 0 of PS/2 packet always has bit 3 set — use as sync marker.
     * Drop bytes that don't look like byte 0 when expecting cycle_==0. */
    if (cycle_ == 0 && !(data & 0x08))
        return;

    pkt_[cycle_] = data;
    cycle_ = (cycle_ + 1) % 3;

    if (cycle_ == 0) {
        u8 b = pkt_[0];

        /* Bits 7 (Y overflow) & 6 (X overflow) — discard entire packet.
         * During fast movement the mouse may overflow its internal counter;
         * the movement values in this packet are invalid. */
        if (b & 0xC0)
            return;

        /* 8-bit signed movement with overflow sign extension */
        int dx = (b & 0x10) ? (int)(pkt_[1] | 0xFFFFFF00) : (int)pkt_[1];
        int dy = (b & 0x20) ? (int)(pkt_[2] | 0xFFFFFF00) : (int)pkt_[2];

        mx_ += dx;
        my_ -= dy;   /* PS/2 Y is inverted */

        if (mx_ < 0) mx_ = 0;
        if (my_ < 0) my_ = 0;
        if (mx_ > 1023) mx_ = 1023;
        if (my_ > 767) my_ = 767;

        mbtn_ = b & 7;
    }
}
