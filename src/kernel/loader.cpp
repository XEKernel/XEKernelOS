#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/task.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR   0x400000
#define USER_STACK_TOP   0xB0000000  /* 4KB user stack at top of user space */
#define USER_STACK_SIZE  0x1000

/* Simple BIN loader: copy to fixed address, create user task */
static int load_bin_task(const char *path) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) { serial_write_str("loader: out of memory\n"); return -1; }

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); serial_write_str("loader: file not found\n"); return -1; }

    /* Create user page directory */
    PagingManager *user_pd = new PagingManager();

    /* Map code region at USER_LOAD_ADDR — copy via physical address first */
    u32 code_pages = (sz + 0xFFF) / 0x1000;
    for (u32 i = 0; i < code_pages; i++) {
        u32 phys = mm_alloc_page();
        u32 virt = USER_LOAD_ADDR + i * 0x1000;

        /* Copy data to physical page (identity-mapped, <16MB) */
        u32 copy_start = i * 0x1000;
        u32 copy_len = ((copy_start + 0x1000) > (u32)sz) ? ((u32)sz - copy_start) : 0x1000;
        u8 *phys_dst = (u8 *)phys;
        for (u32 j = 0; j < copy_len; j++)
            phys_dst[j] = buf[copy_start + j];

        user_pd->map_page(virt, phys, PT_FLAGS);
    }
    kfree(buf);

    /* Map user stack region */
    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        user_pd->map_page(USER_STACK_TOP - (i + 1) * 0x1000, phys, PT_FLAGS);
    }

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

    task_create_user((void *)USER_LOAD_ADDR, USER_STACK_TOP, user_pd);
    task_start_user();
    return 0;
}

int load_elf(const char *path) {
    /* Create user page directory */
    PagingManager *user_pd = new PagingManager();

    /* Load ELF segments and get entry point */
    u32 entry = ElfLoader::load(path, user_pd);
    if (!entry) {
        delete user_pd;
        return -1;
    }

    /* Map user stack region */
    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        user_pd->map_page(USER_STACK_TOP - (i + 1) * 0x1000, phys, PT_FLAGS);
    }

    gfx_puts("Running ELF program...\n");
    task_create_user((void *)entry, USER_STACK_TOP, user_pd);
    task_start_user();
    return 0;
}

int load_binary(const char *path) {
    /* Read first 4 bytes to detect ELF magic */
    u8 magic[4];
    int r = fat_read_file_buf(path, magic, 4);
    if (r < 4) {
        serial_write_str("loader: cannot read file\n");
        return -1;
    }

    /* Check for ELF magic: 0x7F 'E' 'L' 'F' */
    if (magic[0] == 0x7F && magic[1] == 'E' &&
        magic[2] == 'L'  && magic[3] == 'F') {
        return load_elf(path);
    }

    /* Fall back to BIN */
    return load_bin_task(path);
}
