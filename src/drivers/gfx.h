#pragma once
#include "lib/types.h"

#define COLOR_BLACK   0x00
#define COLOR_BLUE    0x01
#define COLOR_GREEN   0x02
#define COLOR_CYAN    0x03
#define COLOR_RED     0x04
#define COLOR_MAGENTA 0x05
#define COLOR_BROWN   0x06
#define COLOR_LGRAY   0x07
#define COLOR_DGRAY   0x08
#define COLOR_LBLUE   0x09
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_LRED    0x0C
#define COLOR_LMAGENTA 0x0D
#define COLOR_YELLOW  0x0E
#define COLOR_WHITE   0x0F

void gfx_init(void);
void gfx_clear(u8 color);
void gfx_set_pixel(int x, int y, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);
void gfx_set_fg(u8 color);
void gfx_set_bg(u8 color);

void gfx_putc(char c);
void gfx_puts(const char *s);
void gfx_puts_utf8(const char *s);
void gfx_cursor_draw(void);
void gfx_cursor_erase(void);
void gfx_put_hex_byte(u8 b);
void gfx_put_hex_u32(u32 v);
