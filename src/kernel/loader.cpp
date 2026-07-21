#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR   0x10000000
#define USER_LOAD_PHYS   0x01000000  /* physical 16MB, within QEMU 32MB */
#define USER_STACK_TOP   0xB0000000
#define USER_STACK_SIZE  0x1000

static int load_bin_task(const char *path) {
    u8 *buf = (u8 *)kmalloc(65536);
    if (!buf) return -1;

    int sz = fat_read_file_buf(path, buf, 65536);
    if (sz <= 0) { kfree(buf); return -1; }

    PagingManager *user_pd = new PagingManager();

    /* Copy code to physical 0x1000000 via identity mapping */
    u8 *phys_dst = (u8 *)USER_LOAD_PHYS;
    for (int i = 0; i < sz; i++) phys_dst[i] = buf[i];
    kfree(buf);

    /* Map as 4MB PSE page with USER flag */
    user_pd->map_user_4mb(USER_LOAD_ADDR, USER_LOAD_PHYS);

    /* Map user stack with a 4KB page table (small, no PSE) */
    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        u32 virt = USER_STACK_TOP - (i+1)*0x1000;
        /* Copy zeros — stack is initially empty */
        u8 *s = (u8 *)phys;
        for (u32 j = 0; j < 0x1000; j++) s[j] = 0;
        user_pd->map_page(virt, phys, PT_FLAGS);
    }

    gfx_puts("Running user program...\n");
    enter_user_mode(USER_LOAD_ADDR, USER_STACK_TOP, user_pd);
    return 0;
}

int load_elf(const char *path) {
    PagingManager *user_pd = new PagingManager();

    /* For ELF, we load to physical 0x1000000, 4MB PSE page */
    u32 entry = ElfLoader::load(path, user_pd);
    if (!entry) { delete user_pd; return -1; }

    for (u32 i = 0; i < USER_STACK_SIZE / 0x1000; i++) {
        u32 phys = mm_alloc_page();
        user_pd->map_page(USER_STACK_TOP - (i+1)*0x1000, phys, PT_FLAGS);
    }

    gfx_puts("Running ELF program...\n");
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
