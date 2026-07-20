#include "drivers/vga.h"
#include "lib/ports.h"

static int cr, cc;

static void vga_set_cursor(void) {
    u16 pos = cr * VGA_COLS + cc;
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}

static void scroll(void) {
    u16 *dst = VGA_BASE, *src = VGA_BASE + VGA_COLS;
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++) dst[i] = src[i];
    for (int i = 0; i < VGA_COLS; i++)
        VGA_BASE[VGA_COLS*(VGA_ROWS-1) + i] = ATTR | ' ';
    cr = VGA_ROWS - 1; cc = 0;
    vga_set_cursor();
}

void vga_clear(void) {
    for (int i = 0; i < VGA_COLS*VGA_ROWS; i++)
        VGA_BASE[i] = ATTR | ' ';
    cr = cc = 0;
    vga_set_cursor();
}

void vga_putc(char ch) {
    if (ch == '\n') { cc = 0; cr++; }
    else if (ch == '\r') cc = 0;
    else if (ch == '\b') { if (cc > 0) cc--; }
    else if (ch == '\t') { cc = (cc + 8) & ~7; if (cc >= VGA_COLS) { cc = 0; cr++; } }
    else if ((u8)ch >= ' ') {
        VGA_BASE[cr * VGA_COLS + cc] = ATTR | (u8)ch;
        cc++;
    }
    if (cc >= VGA_COLS) { cc = 0; cr++; }
    if (cr >= VGA_ROWS) scroll();
    vga_set_cursor();
}

void vga_puts(const char *s) {
    for (int i = 0; s[i]; i++) vga_putc(s[i]);
}

static void draw_border(void) {
    u16 *v = VGA_BASE;
    v[0] = ATTR_GREEN | 0xC9;
    for (int i = 1; i < 79; i++) v[i] = ATTR_GREEN | 0xCD;
    v[79] = ATTR_GREEN | 0xBB;
    for (int r = 1; r < 24; r++) {
        VGA_BASE[r*80]     = ATTR_GREEN | 0xBA;
        VGA_BASE[r*80+79]  = ATTR_GREEN | 0xBA;
    }
    v = VGA_BASE + 24*80;
    v[0] = ATTR_GREEN | 0xC8;
    for (int i = 1; i < 79; i++) v[i] = ATTR_GREEN | 0xCD;
    v[79] = ATTR_GREEN | 0xBC;
}

static void draw_centered(int row, const char *s, u16 attr) {
    int len;
    for (len = 0; s[len]; len++);
    int col = (80 - len) / 2;
    u16 *v = VGA_BASE + row * 80 + col;
    for (int i = 0; s[i]; i++) v[i] = attr | (u8)s[i];
}

void boot_screen(void) {
    vga_clear();
    draw_border();
    draw_centered(12, "XEKernelOS v0.2.0",    ATTR_TITLE);
    draw_centered(14, "XEKernel - x86 Kernel", ATTR);
    draw_centered(18, "Press any key to start...", ATTR);
}
