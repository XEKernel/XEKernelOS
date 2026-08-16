#include "drivers/gfx.h"
#include "drivers/font8x16.h"
#include "drivers/font_cn.h"
#include "drivers/mouse.h"

GfxDriver gfx;

void GfxDriver::init_palette() {
    static const u32 std16[] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF,
    };
    for (int i = 0; i < 16; i++) palette_[i] = std16[i];
    for (int i = 16; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        palette_[i] = (r * 36 << 16) | (g * 36 << 8) | (b * 85);
    }
}

void GfxDriver::init() {
    u32 *vbe = (u32 *)0x500;
    fb_    = (u8 *)vbe[0];
    w_     = (int)vbe[1];
    h_     = (int)vbe[2];
    bpp_   = (int)vbe[3];
    pitch_ = (int)vbe[4];
    int min_pitch = w_ * (bpp_ / 8);
    if (pitch_ < min_pitch) pitch_ = min_pitch;
    cols_  = w_ / FONT_W;
    rows_  = h_ / FONT_H;
    init_palette();
    cx_ = cy_ = 0;
}

void GfxDriver::set_fg(u8 color) { fg_ = color & 0x0F; }
void GfxDriver::set_bg(u8 color) { bg_ = color & 0x0F; }

void GfxDriver::set_pixel(int x, int y, u8 color) {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
    u32 c = palette_[color];
    if (bpp_ == 32) {
        ((u32 *)fb_)[y * (pitch_ / 4) + x] = c;
    } else {
        u8 *p = &fb_[y * pitch_ + x * (bpp_ / 8)];
        p[0] = c & 0xFF;
        p[1] = (c >> 8) & 0xFF;
        p[2] = (c >> 16) & 0xFF;
    }
}

void GfxDriver::fill_rect(int x, int y, int w, int h, u8 color) {
    if (w <= 0 || h <= 0) return;

    /* 裁剪到屏幕范围 — 用户态 ioctl 可传任意坐标, 不裁剪会越界写显存 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= w_ || y >= h_) return;
    if (x + w > w_) w = w_ - x;
    if (y + h > h_) h = h_ - y;
    if (w <= 0 || h <= 0) return;

    u32 c = palette_[color];
    int bytes = bpp_ / 8;
    if (bytes == 4) {
        for (int dy = 0; dy < h; dy++) {
            u32 *row = (u32 *)(fb_ + (y + dy) * pitch_);
            for (int dx = 0; dx < w; dx++)
                row[x + dx] = c;
        }
    } else {
        u8 b0 = c & 0xFF, b1 = (c >> 8) & 0xFF, b2 = (c >> 16) & 0xFF;
        for (int dy = 0; dy < h; dy++) {
            u8 *row = &fb_[(y + dy) * pitch_ + x * bytes];
            for (int dx = 0; dx < w; dx++) {
                row[dx * 3]     = b0;
                row[dx * 3 + 1] = b1;
                row[dx * 3 + 2] = b2;
            }
        }
    }
}

void GfxDriver::clear(u8 color) {
    fill_rect(0, 0, w_, h_, color);
    cx_ = cy_ = 0;
}

void GfxDriver::draw_char(int sx, int sy, char c, u8 fg, u8 bg) {
    if (c < 32 || c > 126) c = '?';
    int idx = (c - 32) * FONT_H;
    for (int row = 0; row < FONT_H; row++) {
        u8 bits = font_8x16[idx + row];
        for (int col = 0; col < FONT_W; col++) {
            u8 color = (bits & (0x80 >> col)) ? fg : bg;
            set_pixel(sx + col, sy + row, color);
        }
    }
}

void GfxDriver::scroll() {
    /* Disable interrupts during scroll to prevent PIT ISR from
       calling mcursor_update() (XOR framebuffer) in the middle
       of the byte-by-byte memmove, which would leave visual artifacts.
       Use pushf/popf to preserve the previous IF state — if scroll
       is called during a syscall (IF already cleared by interrupt gate),
       we must not re-enable interrupts prematurely. */
    __asm__ volatile("pushf; cli");
    int line_h = FONT_H;
    int bytes  = pitch_ * line_h;
    for (int row = 1; row < rows_; row++) {
        u8 *dst = fb_ + (row - 1) * bytes;
        u8 *src = fb_ + row * bytes;
        for (int i = 0; i < bytes; i++) dst[i] = src[i];
    }
    fill_rect(0, (rows_ - 1) * line_h, w_, line_h, bg_);
    cy_ = rows_ - 1;
    cx_ = 0;
    __asm__ volatile("popf");
}

void GfxDriver::putc(char c) {
    if (c == '\n') { cx_ = 0; cy_++; }
    else if (c == '\r') cx_ = 0;
    else if (c == '\b') { if (cx_ > 0) { cx_--; fill_rect(cx_ * FONT_W, cy_ * FONT_H, FONT_W, FONT_H, bg_); } }
    else if (c == '\t') { cx_ = (cx_ + 4) & ~3; if (cx_ >= cols_) { cx_ = 0; cy_++; } }
    else if ((u8)c >= ' ') {
        draw_char(cx_ * FONT_W, cy_ * FONT_H, c, fg_, bg_);
        cx_++;
    }
    if (cx_ >= cols_) { cx_ = 0; cy_++; }
    if (cy_ >= rows_) scroll();
}

void GfxDriver::cursor_draw() {
    int sx = cx_ * FONT_W, sy = cy_ * FONT_H + FONT_H - 2;
    fill_rect(sx, sy, FONT_W, 2, fg_);
}

void GfxDriver::cursor_erase() {
    int sx = cx_ * FONT_W, sy = cy_ * FONT_H;
    draw_char(sx, sy, ' ', fg_, bg_);
}

void GfxDriver::puts(const char *s) {
    for (int i = 0; s[i]; i++) putc(s[i]);
}

void GfxDriver::draw_line(int x1, int y1, int x2, int y2, u8 color) {
    int dx = x2 - x1; if (dx < 0) dx = -dx;
    int dy = y2 - y1; if (dy < 0) dy = -dy;
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        set_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

void GfxDriver::draw_rect(int x, int y, int w, int h, u8 color) {
    draw_line(x, y, x + w - 1, y, color);
    draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    draw_line(x, y + h - 1, x, y, color);
}

void GfxDriver::puts_at(int x, int y, const char *s, u8 color) {
    /* 定位绘制 — 真透明背景: 只画字体前景像素, 不触碰底色。
       旧实现传 bg=0x00 给 draw_char, 每个字符格被涂黑 —
       黑色文字 (如任务栏时钟) 黑字黑底完全隐形, 时钟"永远不动"
       (实测: 秒位每秒重画但屏幕像素恒定)。 */
    int old_cx = cx_, old_cy = cy_, old_fg = fg_;
    fg_ = color;
    int sx = x;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') { x = sx; y += FONT_H; continue; }
        char c = s[i];
        if (c < 32 || c > 126) c = '?';
        int idx = (c - 32) * FONT_H;
        for (int row = 0; row < FONT_H; row++) {
            u8 bits = font_8x16[idx + row];
            for (int col = 0; col < FONT_W; col++) {
                if (bits & (0x80 >> col))
                    set_pixel(x + col, y + row, color);
            }
        }
        x += FONT_W;
    }
    fg_ = old_fg; cx_ = old_cx; cy_ = old_cy;
}

void GfxDriver::put_hex_byte(u8 b) {
    static const char h[] = "0123456789ABCDEF";
    putc(h[b >> 4]);
    putc(h[b & 15]);
}

void GfxDriver::put_hex_u32(u32 v) {
    put_hex_byte((u8)(v >> 24));
    put_hex_byte((u8)(v >> 16));
    put_hex_byte((u8)(v >> 8));
    put_hex_byte((u8)v);
}

/* ---- Chinese font (16x16) support ---- */

const unsigned char *GfxDriver::font_cn_lookup(u16 cp) {
    return font_cn_lookup_impl(cp);
}

void GfxDriver::draw_cn_char(int sx, int sy, u16 cp, u8 fg, u8 bg) {
    const unsigned char *glyph = font_cn_lookup(cp);
    for (int row = 0; row < 16; row++) {
        u8 b0, b1;
        if (glyph) {
            b0 = glyph[row * 2];
            b1 = glyph[row * 2 + 1];
        } else {
            b0 = (row == 0 || row == 15) ? 0xFF : 0x81;
            b1 = (row == 0 || row == 15) ? 0xFF : 0x81;
        }
        for (int col = 0; col < 16; col++) {
            u8 bit = (col < 8) ? (b0 >> (7 - col)) : (b1 >> (15 - col));
            u8 color = (bit & 1) ? fg : bg;
            set_pixel(sx + col, sy + row, color);
        }
    }
}

int GfxDriver::utf8_decode(const char *s, u16 *cp) {
    u8 c = (u8)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    /* 续字节存在才按多字节解码, 防止字符串末尾截断时越界读 */
    if ((c & 0xE0) == 0xC0 && s[1]) { *cp = (u16)(((c & 0x1F) << 6) | (s[1] & 0x3F)); return 2; }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) { *cp = (u16)(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F)); return 3; }
    *cp = c; return 1;
}

void GfxDriver::puts_utf8(const char *s) {
    int i = 0;
    while (s[i]) {
        u16 cp;
        int len = utf8_decode(s + i, &cp);
        if (len == 1 && cp < 0x80) {
            putc((char)cp);
        } else {
            if (cx_ >= cols_ - 1) { cx_ = 0; cy_++; }
            if (cy_ >= rows_) scroll();
            draw_cn_char(cx_ * FONT_W, cy_ * FONT_H, cp, fg_, bg_);
            cx_ += 2;
            if (cx_ >= cols_) { cx_ = 0; cy_++; }
            if (cy_ >= rows_) scroll();
        }
        i += len;
    }
}

void GfxDriver::mcursor_restore() {
    for (int y = 0; y < cur_h_; y++) {
        int fy = cur_y_ + y;
        if (fy < 0 || fy >= h_) continue;
        for (int x = 0; x < cur_w_; x++) {
            int fx = cur_x_ + x;
            if (fx < 0 || fx >= w_) continue;
            int off = fy * pitch_ + fx * 4;
            *(u32 *)(fb_ + off) = cur_save_[y * cur_w_ + x];
        }
    }
}

void GfxDriver::mcursor_draw() {
    if (w_ <= 0 || h_ <= 0) return;

    int mx, my, mb;
    mouse_get(&mx, &my, &mb);
    cur_x_ = mx - 4; if (cur_x_ < 0) cur_x_ = 0;
    cur_y_ = my - 4; if (cur_y_ < 0) cur_y_ = 0;
    if (cur_x_ + 8 > w_) cur_x_ = w_ - 8;
    if (cur_y_ + 8 > h_) cur_y_ = h_ - 8;

    /* Save pixels under cursor, then draw 8x8 red box */
    for (int y = 0; y < 8; y++) {
        int fy = cur_y_ + y;
        for (int x = 0; x < 8; x++) {
            int fx = cur_x_ + x;
            int off = fy * pitch_ + fx * 4;
            cur_save_[y * 8 + x] = *(u32 *)(fb_ + off);
            *(u32 *)(fb_ + off) = 0x00FF0000;
        }
    }
}

void GfxDriver::mcursor_update() {
    if (w_ <= 0 || h_ <= 0) return;

    int old_x = cur_x_, old_y = cur_y_;

    /* XOR-out old cursor (hollow 8x8 box, 1px border) */
    if (old_x >= 0 && old_y >= 0) {
        for (int y = 0; y < 8; y++) {
            int fy = old_y + y;
            if (fy < 0 || fy >= h_) continue;
            for (int x = 0; x < 8; x++) {
                if (x > 0 && x < 7 && y > 0 && y < 7) continue; /* hollow */
                int fx = old_x + x;
                if (fx < 0 || fx >= w_) continue;
                int off = fy * pitch_ + fx * 4;
                *(u32 *)(fb_ + off) ^= 0x00FFFFFF;  /* XOR white */
            }
        }
    }

    /* Get new position */
    int mx, my, mb;
    mouse_get(&mx, &my, &mb);
    cur_x_ = mx - 4; if (cur_x_ < 0) cur_x_ = 0;
    cur_y_ = my - 4; if (cur_y_ < 0) cur_y_ = 0;
    if (cur_x_ + 8 > w_) cur_x_ = w_ - 8;
    if (cur_y_ + 8 > h_) cur_y_ = h_ - 8;

    /* XOR-in new cursor */
    for (int y = 0; y < 8; y++) {
        int fy = cur_y_ + y;
        for (int x = 0; x < 8; x++) {
            if (x > 0 && x < 7 && y > 0 && y < 7) continue;
            int fx = cur_x_ + x;
            int off = fy * pitch_ + fx * 4;
            *(u32 *)(fb_ + off) ^= 0x00FFFFFF;
        }
    }
}
