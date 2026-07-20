#include "kernel/loader.h"
#include "kernel/user.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR 0x400000

int load_binary(const char *path) {
    u8 *buf = kmalloc(65536);
    if (!buf) { serial_write_str("loader: out of memory\n"); return -1; }

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); return -1; }

    u8 *load_addr = (u8 *)USER_LOAD_ADDR;
    for (int i = 0; i < sz; i++) load_addr[i] = buf[i];
    kfree(buf);

    serial_write_str("loader: loaded ");
    serial_write_str(path);
    serial_write_str(" (");
    serial_write_char('0' + (sz / 10000) % 10);
    serial_write_char('0' + (sz / 1000) % 10);
    serial_write_char('0' + (sz / 100) % 10);
    serial_write_char('0' + (sz / 10) % 10);
    serial_write_char('0' + sz % 10);
    serial_write_str(" bytes)\n");
    gfx_puts("Running user program...\n");
    user_run((void (*)(void))USER_LOAD_ADDR);
    return 0;
}
