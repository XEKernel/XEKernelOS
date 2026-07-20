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
