#include "drivers/serial.h"
#include "lib/ports.h"

#define COM1 0x3F8

int serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    return 0;
}

void serial_write_char(char c) {
    for (int i = 0; i < 30000; i++)
        if (inb(COM1 + 5) & 0x20) break;
    outb(COM1, c);
}

void serial_write_str(const char *s) {
    while (*s) serial_write_char(*s++);
}
