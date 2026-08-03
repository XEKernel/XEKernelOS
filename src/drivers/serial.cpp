#include "drivers/serial.h"

SerialPort com1(0x3F8);

void SerialPort::init() {
    write_reg(1, 0x00);   /* disable interrupts */
    write_reg(3, 0x80);   /* DLAB = 1 */
    write_reg(0, 0x03);   /* divisor low = 3 (38400 baud) */
    write_reg(1, 0x00);   /* divisor high = 0 */
    write_reg(3, 0x03);   /* 8N1, DLAB = 0 */
    write_reg(2, 0xC7);   /* FIFO enable, clear, 14-byte trigger */
    write_reg(4, 0x0B);   /* DTR, RTS, OUT2 */
}

void SerialPort::putc(char c) {
    for (volatile int i = 0; i < 10000000; i++)
        if (read_reg(5) & 0x20) break;
    write_reg(0, (u8)c);
}

void SerialPort::puts(const char *s) {
    while (*s) putc(*s++);
}

void SerialPort::put_hex_byte(u8 b) {
    static const char h[] = "0123456789ABCDEF";
    putc(h[b >> 4]);
    putc(h[b & 15]);
}

void SerialPort::put_hex_u32(u32 v) {
    put_hex_byte((u8)(v >> 24));
    put_hex_byte((u8)(v >> 16));
    put_hex_byte((u8)(v >> 8));
    put_hex_byte((u8)v);
}
