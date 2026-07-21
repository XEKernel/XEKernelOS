#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "shell/shell.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR 0x400000

static int count_args(const char *s) {
    int n = 0, in_word = 0;
    while (*s) {
        if (*s == ' ') { in_word = 0; s++; continue; }
        if (!in_word) { n++; in_word = 1; }
        s++;
    }
    return n;
}

static void fail(const char *msg) {
    gfx_set_fg(COLOR_LRED);
    gfx_puts(msg);
    gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
    serial_write_str(msg);
    serial_write_char('\n');
}

static int load_flat_binary(const char *path, const char *args) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) { fail("loader: out of memory"); return -1; }

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); fail("loader: file not found"); return -1; }

    u8 *dst = (u8 *)USER_LOAD_ADDR;
    for (int i = 0; i < sz; i++) dst[i] = buf[i];
    kfree(buf);

    serial_write_str("loader: loaded flat binary (");
    serial_write_char('0' + (sz / 10000) % 10);
    serial_write_char('0' + (sz / 1000) % 10);
    serial_write_char('0' + (sz / 100) % 10);
    serial_write_char('0' + (sz / 10) % 10);
    serial_write_char('0' + sz % 10);
    serial_write_str(" bytes)\n");

    PagingManager *user_pd = new PagingManager();
    gfx_puts("Running user program...\n");
    int ac = args ? count_args(args) : 0;
    enter_user_mode(USER_LOAD_ADDR, 0, user_pd, ac, args);
    shell_redraw();
    gfx_putc('\n');
    return 0;
}

static int load_elf_binary(const char *path, const char *args) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) { fail("loader: out of memory"); return -1; }

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); fail("loader: file not found"); return -1; }
    if (sz < 52)   { kfree(buf); fail("elf: file too small"); return -1; }

    if (buf[0]!=0x7F || buf[1]!='E' || buf[2]!='L' || buf[3]!='F') {
        kfree(buf);
        return load_flat_binary(path, args);
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)buf;
    if (ehdr->e_type != 2) {
        kfree(buf);
        fail("elf: not an executable");
        return -1;
    }
    if (ehdr->e_machine != 3) {
        kfree(buf);
        fail("elf: not i386");
        return -1;
    }
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        kfree(buf);
        fail("elf: no program headers");
        return -1;
    }

    u32 entry = ehdr->e_entry;

    serial_write_str("elf: entry=0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int j = 28; j >= 0; j -= 4) serial_write_char(hex[(entry >> j) & 15]);
    serial_write_str("\n");

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(buf + ehdr->e_phoff);
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != 1) continue;
        if (!(ph->p_flags & 3)) continue;

        serial_write_str("elf: segment vaddr=0x");
        for (int j = 28; j >= 0; j -= 4) serial_write_char(hex[(ph->p_vaddr >> j) & 15]);
        serial_write_str("\n");

        u8 *dst = (u8 *)ph->p_vaddr;
        u32 copy_len = ph->p_filesz;
        if (ph->p_offset + copy_len > (u32)sz) copy_len = (u32)sz - ph->p_offset;
        for (u32 k = 0; k < copy_len; k++)
            dst[k] = buf[ph->p_offset + k];
        for (u32 k = copy_len; k < ph->p_memsz; k++)
            dst[k] = 0;
    }

    kfree(buf);
    PagingManager *user_pd = new PagingManager();
    gfx_puts("Running ELF program...\n");
    int ac = args ? count_args(args) : 0;
    enter_user_mode(entry, 0, user_pd, ac, args);
    shell_redraw();
    gfx_putc('\n');
    return 0;
}

int load_binary(const char *path, const char *args) {
    return load_elf_binary(path, args);
}
