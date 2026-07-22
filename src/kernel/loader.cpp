#include "kernel/loader.h"
#include "kernel/elf.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/task.h"
#include "shell/shell.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/gfx.h"

#define USER_LOAD_ADDR 0x400000
#define USER_STACK_TOP 0x420000
#define USER_STACK_SZ  0x10000   /* 64KB */

/* Map a range of pages into the user page directory.
   Uses identity mapping (virt == phys). */
static void map_user_pages(PagingManager *pd, u32 vaddr, u32 size) {
    u32 pages = (size + 0xFFF) / 0x1000;
    for (u32 i = 0; i < pages; i++)
        pd->map_page(vaddr + i * 0x1000, vaddr + i * 0x1000, PT_FLAGS);
}

/* Map framebuffer as 4MB PSE page with PAGE_USER so ring3 can
   access it directly (for graphics programs). */
static void map_user_fb(PagingManager *pd) {
    u32 fbaddr = *(u32 *)0x500;
    pd->map_user_4mb(fbaddr, fbaddr);
}

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

    /* Copy binary to load address */
    u8 *dst = (u8 *)USER_LOAD_ADDR;
    for (int i = 0; i < sz; i++) dst[i] = buf[i];
    kfree(buf);

    serial_write_str("loader: flat binary ");
    serial_write_char('0' + (sz / 10000) % 10);
    serial_write_char('0' + (sz / 1000) % 10);
    serial_write_char('0' + (sz / 100) % 10);
    serial_write_char('0' + (sz / 10) % 10);
    serial_write_char('0' + sz % 10);
    serial_write_str("B\n");

    __asm__ volatile("cli");  /* prevent PIT preemption during init */

    PagingManager *user_pd = new PagingManager();
    /* Map binary pages — at least 4KB so .bss is covered */
    u32 map_sz = (u32)sz;
    if (map_sz < 0x1000) map_sz = 0x1000;
    map_user_pages(user_pd, USER_LOAD_ADDR, map_sz);
    map_user_pages(user_pd, USER_STACK_TOP - USER_STACK_SZ, USER_STACK_SZ);
    map_user_fb(user_pd);  /* so ring3 can access framebuffer */

    task_create_user((void *)USER_LOAD_ADDR, USER_STACK_TOP, user_pd);
    if (current_task) current_task->state = TASK_RUNNING;

    int ac = args ? count_args(args) : 0;
    enter_user_mode(USER_LOAD_ADDR, USER_STACK_TOP, user_pd, ac, args);

    __asm__ volatile("sti");
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
    u32 max_vaddr = 0;

    serial_write_str("elf: entry=0x");
    for (int j = 28; j >= 0; j -= 4)
        serial_write_char("0123456789ABCDEF"[(entry >> j) & 15]);
    serial_write_str("\n");

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(buf + ehdr->e_phoff);
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != 1) continue;
        if (!(ph->p_flags & 3)) continue;

        u32 seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end > max_vaddr) max_vaddr = seg_end;

        serial_write_str("elf: seg v=0x");
        for (int j = 28; j >= 0; j -= 4)
            serial_write_char("0123456789ABCDEF"[(ph->p_vaddr >> j) & 15]);
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

    __asm__ volatile("cli");

    PagingManager *user_pd = new PagingManager();
    /* Map from the lowest segment vaddr up through max_vaddr + stack */
    u32 load_base = USER_LOAD_ADDR;
    u32 total_size = max_vaddr - load_base;
    if (total_size > 0)
        map_user_pages(user_pd, load_base, total_size);
    map_user_pages(user_pd, USER_STACK_TOP - USER_STACK_SZ, USER_STACK_SZ);
    map_user_fb(user_pd);  /* so ring3 can access framebuffer directly */

    task_create_user((void *)entry, USER_STACK_TOP, user_pd);
    if (current_task) current_task->state = TASK_RUNNING;

    int ac = args ? count_args(args) : 0;
    enter_user_mode(entry, USER_STACK_TOP, user_pd, ac, args);

    __asm__ volatile("sti");
    shell_redraw();
    gfx_putc('\n');
    return 0;
}

int load_binary(const char *path, const char *args) {
    return load_elf_binary(path, args);
}
