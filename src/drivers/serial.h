#pragma once
#include "lib/types.h"
#include "lib/ports.h"

class SerialPort {
public:
    SerialPort(u16 base) : port_(base) {}

    void init();
    void putc(char c);
    void puts(const char *s);

    /* hex dump helpers */
    void put_hex_byte(u8 b);
    void put_hex_u32(u32 v);

    /* Input (用于串口控制台 / QEMU 测试注入) */
    bool has_data() { return (read_reg(5) & 0x01) != 0; }
    char read_char() { return (char)read_reg(0); }

private:
    u16 port_;
    u8  read_reg(u8 offset) { return inb(port_ + offset); }
    void write_reg(u8 offset, u8 val) { outb(port_ + offset, val); }
};

/* Global instance for COM1 */
extern SerialPort com1;

/* C-compat wrappers for existing code */
inline int  serial_init()            { com1.init(); return 0; }
inline void serial_write_char(char c) { com1.putc(c); }
inline void serial_write_str(const char *s) { com1.puts(s); }
inline void serial_write_str_len(const char *s, u32 len) {
    for (u32 i = 0; i < len; i++) com1.putc(s[i]);
}
inline bool serial_has_data() { return com1.has_data(); }
inline char serial_read_char() { return com1.read_char(); }
inline void serial_write_u32(u32 v) { com1.put_hex_u32(v); }
