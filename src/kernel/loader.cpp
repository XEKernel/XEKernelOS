#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/user.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

/* Load code at 0x400000 via kernel identity mapping (PDE[1] with USER).
   Matches original user_run() approach. No CR3 switch needed. */
#define USER_LOAD_ADDR 0x400000

int load_elf(const char *path) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) return -1;

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); return -1; }

    /* Check ELF magic */
    if (sz < 52 || buf[0] != 0x7F || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        /* Not ELF — treat as flat binary */
        u8 *dst = (u8 *)USER_LOAD_ADDR;
        for (int i = 0; i < sz; i++) dst[i] = buf[i];
        kfree(buf);
        gfx_puts("Running user program...\n");
        enter_user_mode(USER_LOAD_ADDR, 0, nullptr);
        return 0;
    }

    /* Simple ELF loader: find first PT_LOAD executable segment */
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)buf;
    u32 entry = ehdr->e_entry;

    serial_write_str("elf: entry=0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int j = 28; j >= 0; j -= 4) serial_write_char(hex[(entry >> j) & 15]);
    serial_write_str("\n");

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(buf + ehdr->e_phoff);
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != 1) continue;  /* PT_LOAD */
        if (!(ph->p_flags & 1)) continue;  /* must be executable */

        serial_write_str("elf: segment vaddr=0x");
        for (int j = 28; j >= 0; j -= 4) serial_write_char(hex[(ph->p_vaddr >> j) & 15]);
        serial_write_str("\n");

        /* Copy directly to virtual address (kernel identity-mapped) */
        u8 *dst = (u8 *)ph->p_vaddr;
        u32 copy_len = ph->p_filesz;
        if (ph->p_offset + copy_len > (u32)sz) copy_len = (u32)sz - ph->p_offset;
        for (u32 k = 0; k < copy_len; k++)
            dst[k] = buf[ph->p_offset + k];

        /* Zero BSS */
        for (u32 k = copy_len; k < ph->p_memsz; k++)
            dst[k] = 0;
    }

    kfree(buf);
    gfx_puts("Running ELF program...\n");
    enter_user_mode(entry, 0, nullptr);
    return 0;
}

int load_binary(const char *path) {
    return load_elf(path);  /* handle both ELF and BIN */
}
