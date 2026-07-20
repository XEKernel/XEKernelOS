#pragma once
#include "lib/types.h"

void gfx_init(void);
void gfx_clear(u8 color);
void gfx_set_pixel(int x, int y, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);

void gfx_putc(char c);
void gfx_puts(const char *s);
void gfx_put_hex_byte(u8 b);
void gfx_put_hex_u32(u32 v);
