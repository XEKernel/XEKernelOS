#include "kernel/panic.h"
#include "drivers/gfx.h"
#include "drivers/serial.h"

static void put_hex_u32_serial(u32 v) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_write_char(h[(v >> i) & 15]);
}

void kernel_panic(registers_t *regs, const char *msg) {
    __asm__("cli");
    gfx_puts("\nKERNEL PANIC: ");
    gfx_puts(msg);
    gfx_putc('\n');
    serial_write_str("\nKERNEL PANIC: ");
    serial_write_str(msg);
    serial_write_char('\n');
    if (regs) {
        gfx_puts("EIP=0x"); gfx_put_hex_u32(regs->eip);
        gfx_puts(" CS=0x"); gfx_put_hex_u32(regs->cs);
        gfx_puts(" EFLAGS=0x"); gfx_put_hex_u32(regs->eflags);
        gfx_puts(" ESP=0x"); gfx_put_hex_u32(regs->_esp);
        gfx_puts(" EBP=0x"); gfx_put_hex_u32(regs->ebp);
        gfx_puts(" VEC="); gfx_put_hex_u32(regs->vec);
        gfx_puts(" ERR="); gfx_put_hex_u32(regs->err_code);
        gfx_putc('\n');
        serial_write_str("EIP=0x"); put_hex_u32_serial(regs->eip);
        serial_write_str(" CS=0x"); put_hex_u32_serial(regs->cs);
        serial_write_str(" EFLAGS=0x"); put_hex_u32_serial(regs->eflags);
        serial_write_str(" ESP=0x"); put_hex_u32_serial(regs->_esp);
        serial_write_str(" EBP=0x"); put_hex_u32_serial(regs->ebp);
        serial_write_char('\n');
    }
    while (1) __asm__ volatile("hlt");
}
