#include "kernel/syscall.h"
#include "drivers/serial.h"

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    for (u32 i = 0; i < len; i++) serial_write_char(str[i]);
    serial_write_char('\n');
    r->eax = len;
}

extern "C" void syscall_handler(registers_t *r) {
    serial_write_char('[');
    serial_write_char('0' + (r->eax / 10) % 10);
    serial_write_char('0' + r->eax % 10);
    serial_write_char(']');
    switch (r->eax) {
    case SYS_WRITE:
        sys_write(r);
        break;
    default:
        r->eax = -1;
        break;
    }
}
