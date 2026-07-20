#include "drivers/gfx.h"
#include "drivers/font8x16.h"
#include "drivers/font_cn.h"

#define FONT_W   10
#define FONT_H   16
#define FONT_CN_W 16

static u8  *gfx_fb;
static int  gfx_w, gfx_h, gfx_pitch, gfx_bpp;
static int  gfx_cols, gfx_rows;
static int  cx, cy;
static u8   fg_color = 0x0F;
static u8   bg_color = 0x00;

static u32 palette[256];

static void init_palette(void) {
    static const u32 std16[] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF,
    };
    for (int i = 0; i < 16; i++) palette[i] = std16[i];
    for (int i = 16; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        palette[i] = (r * 36 << 16) | (g * 36 << 8) | (b * 85);
    }
}

void gfx_init(void) {
    u32 *vbe = (u32 *)0x500;
    gfx_fb    = (u8 *)vbe[0];
    gfx_w     = (int)vbe[1];
    gfx_h     = (int)vbe[2];
    gfx_bpp   = (int)vbe[3];
    gfx_pitch = (int)vbe[4];
    int min_pitch = gfx_w * (gfx_bpp / 8);
    if (gfx_pitch < min_pitch) gfx_pitch = min_pitch;
    gfx_cols  = gfx_w / FONT_W;
    gfx_rows  = gfx_h / FONT_H;
    init_palette();
    cx = cy = 0;
}

void gfx_set_fg(u8 color) {
    fg_color = color & 0x0F;
}

void gfx_set_bg(u8 color) {
    bg_color = color & 0x0F;
}

void gfx_set_pixel(int x, int y, u8 color) {
    if (x < 0 || x >= gfx_w || y < 0 || y >= gfx_h) return;
    u32 c = palette[color];
    if (gfx_bpp == 32) {
        ((u32 *)gfx_fb)[y * (gfx_pitch / 4) + x] = c;
    } else {
        u8 *p = &gfx_fb[y * gfx_pitch + x * (gfx_bpp / 8)];
        p[0] = c & 0xFF;
        p[1] = (c >> 8) & 0xFF;
        p[2] = (c >> 16) & 0xFF;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, u8 color) {
    u32 c = palette[color];
    int bytes = gfx_bpp / 8;
    if (bytes == 4) {
        for (int dy = 0; dy < h; dy++) {
            u32 *row = (u32 *)(gfx_fb + (y + dy) * gfx_pitch);
            for (int dx = 0; dx < w; dx++)
                row[x + dx] = c;
        }
    } else {
        u8 b0 = c & 0xFF, b1 = (c >> 8) & 0xFF, b2 = (c >> 16) & 0xFF;
        for (int dy = 0; dy < h; dy++) {
            u8 *row = &gfx_fb[(y + dy) * gfx_pitch + x * bytes];
            for (int dx = 0; dx < w; dx++) {
                row[dx * 3]     = b0;
                row[dx * 3 + 1] = b1;
                row[dx * 3 + 2] = b2;
            }
        }
    }
}

void gfx_clear(u8 color) {
    gfx_fill_rect(0, 0, gfx_w, gfx_h, color);
    cx = cy = 0;
}

static void draw_char(int sx, int sy, char c, u8 fg, u8 bg) {
    if (c < 32 || c > 126) c = '?';
    int idx = (c - 32) * FONT_H;
    for (int row = 0; row < FONT_H; row++) {
        u8 bits = font_8x16[idx + row];
        for (int col = 0; col < FONT_W; col++) {
            u8 color = (bits & (0x80 >> col)) ? fg : bg;
            gfx_set_pixel(sx + col, sy + row, color);
        }
    }
}

static void gfx_scroll(void) {
    int line_h = FONT_H;
    int bytes  = gfx_pitch * line_h;
    for (int row = 1; row < gfx_rows; row++) {
        u8 *dst = gfx_fb + (row - 1) * bytes;
        u8 *src = gfx_fb + row * bytes;
        for (int i = 0; i < bytes; i++) dst[i] = src[i];
    }
    gfx_fill_rect(0, (gfx_rows - 1) * line_h, gfx_w, line_h, bg_color);
    cy = gfx_rows - 1;
    cx = 0;
}

void gfx_putc(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') cx = 0;
    else if (c == '\b') { if (cx > 0) { cx--; gfx_fill_rect(cx * FONT_W, cy * FONT_H, FONT_W, FONT_H, bg_color); } }
    else if (c == '\t') { cx = (cx + 4) & ~3; if (cx >= gfx_cols) { cx = 0; cy++; } }
    else if ((u8)c >= ' ') {
        draw_char(cx * FONT_W, cy * FONT_H, c, fg_color, bg_color);
        cx++;
    }
    if (cx >= gfx_cols) { cx = 0; cy++; }
    if (cy >= gfx_rows) gfx_scroll();
}

void gfx_cursor_draw(void) {
    int sx = cx * FONT_W, sy = cy * FONT_H + FONT_H - 2;
    gfx_fill_rect(sx, sy, FONT_W, 2, fg_color);
}

void gfx_cursor_erase(void) {
    int sx = cx * FONT_W, sy = cy * FONT_H;
    draw_char(sx, sy, ' ', fg_color, bg_color);
}

void gfx_puts(const char *s) {
    for (int i = 0; s[i]; i++) gfx_putc(s[i]);
}

void gfx_put_hex_byte(u8 b) {
    static const char h[] = "0123456789ABCDEF";
    gfx_putc(h[b >> 4]);
    gfx_putc(h[b & 15]);
}

void gfx_put_hex_u32(u32 v) {
    gfx_put_hex_byte((v >> 24) & 0xFF);
    gfx_put_hex_byte((v >> 16) & 0xFF);
    gfx_put_hex_byte((v >> 8) & 0xFF);
    gfx_put_hex_byte(v & 0xFF);
}

/* ---- Chinese font (16x16) support ---- */

static const unsigned char *font_cn_lookup(u16 cp) {
    int lo = 0, hi = FONT_CN_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_cn_codepoint[mid] == cp)
            return &font_cn_data[mid * 32];
        else if (font_cn_codepoint[mid] < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static void draw_cn_char(int sx, int sy, u16 cp, u8 fg, u8 bg) {
    const unsigned char *glyph = font_cn_lookup(cp);
    for (int row = 0; row < 16; row++) {
        u8 b0, b1;
        if (glyph) {
            b0 = glyph[row * 2];
            b1 = glyph[row * 2 + 1];
        } else {
            /* placeholder: hollow box */
            b0 = (row == 0 || row == 15) ? 0xFF : 0x81;
            b1 = (row == 0 || row == 15) ? 0xFF : 0x81;
        }
        for (int col = 0; col < 16; col++) {
            u8 bit = (col < 8) ? (b0 >> (7 - col)) : (b1 >> (15 - col));
            u8 color = (bit & 1) ? fg : bg;
            gfx_set_pixel(sx + col, sy + row, color);
        }
    }
}

static int utf8_decode(const char *s, u16 *cp) {
    u8 c = (u8)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { *cp = (u16)(((c & 0x1F) << 6) | (s[1] & 0x3F)); return 2; }
    if ((c & 0xF0) == 0xE0) { *cp = (u16)(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F)); return 3; }
    *cp = c; return 1;
}

void gfx_puts_utf8(const char *s) {
    int i = 0;
    while (s[i]) {
        u16 cp;
        int len = utf8_decode(s + i, &cp);
        if (len == 1 && cp < 0x80) {
            gfx_putc((char)cp);
        } else {
            /* Chinese char: 2 ASCII cells wide */
            if (cx >= gfx_cols - 1) { cx = 0; cy++; }
            if (cy >= gfx_rows) gfx_scroll();
            draw_cn_char(cx * FONT_W, cy * FONT_H, cp, fg_color, bg_color);
            cx += 2;
            if (cx >= gfx_cols) { cx = 0; cy++; }
            if (cy >= gfx_rows) gfx_scroll();
        }
        i += len;
    }
}
