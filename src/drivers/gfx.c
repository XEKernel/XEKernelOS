#include "drivers/gfx.h"
#include "drivers/font8x16.h"

#define FONT_W  8
#define FONT_H  16

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
    else if (c == '\b') { if (cx > 0) cx--; }
    else if (c == '\t') { cx = (cx + 4) & ~3; if (cx >= gfx_cols) { cx = 0; cy++; } }
    else if ((u8)c >= ' ') {
        draw_char(cx * FONT_W, cy * FONT_H, c, fg_color, bg_color);
        cx++;
    }
    if (cx >= gfx_cols) { cx = 0; cy++; }
    if (cy >= gfx_rows) gfx_scroll();
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
