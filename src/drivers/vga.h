#pragma once
#include "lib/types.h"

#define VGA_BASE   ((u16 *)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25
#define ATTR       0x0F00
#define ATTR_TITLE 0x0E00
#define ATTR_GREEN 0x0A00

void vga_clear(void);
void vga_putc(char ch);
void vga_puts(const char *s);
void boot_screen(void);
