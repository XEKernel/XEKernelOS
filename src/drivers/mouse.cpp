#include "drivers/mouse.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"
#include "drivers/input.h"
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

    mwrite(0xE8); mwrite(0x03);    /* set resolution: 8 counts/mm */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);

    mwrite(0xE6);                   /* set scaling 1:1 */
    if (rready()) inb(0x60);

    mwrite(0xF3); mwrite(200);     /* wheel enable sequence: 200 */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);
    mwrite(0xF3); mwrite(100);     /* 100 */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);
    mwrite(0xF3); mwrite(80);      /* 80 → device switches to 4-byte w/ wheel */
    if (rready()) inb(0x60);
    if (rready()) inb(0x60);
    mwrite(0xF2);                  /* read device ID */
    if (rready()) inb(0x60);       /* ack */
    if (rready()) {
        u8 id = inb(0x60);
        if (id == 3) wheel_mode_ = 1;   /* wheel present → 4-byte packets */
    }

    mwrite(0xF4);                   /* enable reporting */
    if (!rready()) { serial_write_str("mouse: no ack to enable\n"); return; }
    inb(0x60);

    cycle_ = 0;
    /* 初始位置屏幕中央 — (0,0) 角落的光标用户几乎不可见 */
    mx_ = gfx.fb_width() / 2;
    my_ = gfx.fb_height() / 2;
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

    int psz = wheel_mode_ ? 4 : 3;
    pkt_[cycle_] = data;
    cycle_ = (cycle_ + 1) % psz;

    if (cycle_ == 0) {
        u8 b = pkt_[0];

        /* Bits 7 (Y overflow) & 6 (X overflow) — discard entire packet. */
        if (b & 0xC0)
            return;

        /* 8-bit signed movement with overflow sign extension */
        int dx = (b & 0x10) ? (int)(pkt_[1] | 0xFFFFFF00) : (int)pkt_[1];
        int dy = (b & 0x20) ? (int)(pkt_[2] | 0xFFFFFF00) : (int)pkt_[2];

        /* Apply sensitivity: 1x base + light acceleration for fast moves */
        if (dx > 4 || dx < -4) { dx = (dx * 3) / 2; }
        if (dy > 4 || dy < -4) { dy = (dy * 3) / 2; }

        mx_ += dx;
        my_ -= dy;   /* PS/2 Y is inverted */

        if (mx_ < 0) mx_ = 0;
        if (my_ < 0) my_ = 0;
        int mw = gfx.fb_width() - 1;
        int mh = gfx.fb_height() - 1;
        if (mw < 0) mw = 1023;
        if (mh < 0) mh = 767;
        if (mx_ > mw) mx_ = mw;
        if (my_ > mh) my_ = mh;

        int old_btn = mbtn_;
        mbtn_ = b & 7;

        /* 压入统一输入事件流 */
        input_push(1, mx_, my_, (u32)mbtn_);
        if (mbtn_ != old_btn)
            input_push(2, mx_, my_, (u32)mbtn_);

        /* 滚轮: 第4字节有符号 delta (正=上滚, 负=下滚) */
        if (wheel_mode_) {
            int wheel = (int)(signed char)pkt_[3];
            if (wheel)
                input_push(4, mx_, my_, (u32)wheel);
        }
    }
}
