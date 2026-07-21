#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR   0x08000000
#define USER_STACK_TOP   0xB0000000
#define USER_STACK_SIZE  0x1000

static int load_bin_task(const char *path) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) return -1;

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); return -1; }

    PagingManager *user_pd = new PagingManager();

    u32 code_pages = (sz + 0xFFF) / 0x1000;
    for (u32 i = 0; i < code_pages; i++) {
        u32 phys = mm_alloc_page();
        u8 *dst  = (u8 *)phys;
        u32 off  = i * 0x1000;
        u32 len  = ((off + 0x1000) > (u32)sz) ? ((u32)sz - off) : 0x1000;
        for (u32 j = 0; j < len; j++) dst[j] = buf[off + j];
        user_pd->map_page(USER_LOAD_ADDR + off, phys, PT_FLAGS);
    }
    kfree(buf);

    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        user_pd->map_page(USER_STACK_TOP - (i+1)*0x1000, phys, PT_FLAGS);
    }

    gfx_puts("Running user program...\n");
    enter_user_mode(USER_LOAD_ADDR, USER_STACK_TOP, user_pd);
    return 0;
}

int load_elf(const char *path) {
    PagingManager *user_pd = new PagingManager();

    u32 entry = ElfLoader::load(path, user_pd);
    if (!entry) { delete user_pd; return -1; }

    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        user_pd->map_page(USER_STACK_TOP - (i+1)*0x1000, phys, PT_FLAGS);
    }

    gfx_puts("Running ELF program...\n");
    __asm__ volatile("movb $'M', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    enter_user_mode(entry, USER_STACK_TOP, user_pd);
    return 0;
}

int load_binary(const char *path) {
    u8 magic[4];
    if (fat_read_file_buf(path, magic, 4) < 4) return -1;
    if (magic[0]==0x7F && magic[1]=='E' && magic[2]=='L' && magic[3]=='F')
        return load_elf(path);
    return load_bin_task(path);
}
