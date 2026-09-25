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
/* 用户栈顶 — 必须远离代码/.bss 区: 平坦二进制不携带 .bss 大小,
   只能预留 headroom (见 map_sz)。旧值 0x420000 距装载点仅 128KB,
   桌面 (代码 39KB + .bss 60KB) 的 wins 数组直接压进栈区, 栈一长
   就踩坏全局数组。抬高到 0x440000 后代码/.bss 可用 192KB。 */
#define USER_STACK_TOP 0x440000
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
    serial_write_str("launch: pd=0x");
    serial_write_u32((u32)user_pd);
    serial_write_char('\n');
    /* Map binary pages — plus .bss headroom (flat binary 的 .bss 不占文件,
       但全局数组/缓冲占内存 — 桌面 wins 等大数组需额外虚址空间).
       至少 4KB 起. */
    u32 map_sz = (u32)sz + 0x10000;   /* +64KB .bss 预留 */
    /* 代码/.bss 区不得侵入用户栈区, 否则栈生长会踩坏全局数据 */
    if (USER_LOAD_ADDR + map_sz > USER_STACK_TOP - USER_STACK_SZ) {
        delete user_pd;
        fail("loader: program too large");
        return -1;
    }
    map_user_pages(user_pd, USER_LOAD_ADDR, map_sz);
    map_user_pages(user_pd, USER_STACK_TOP - USER_STACK_SZ, USER_STACK_SZ);
    map_user_fb(user_pd);  /* so ring3 can access framebuffer */

    /* 平坦二进制不携带 .bss 信息 — 装载区尾部必须显式清零。
       否则零初始化的全局变量会残留"上一个程序"留在这批物理页里的字节
       (实测: 用户 Shell 的 cur_dir_cluster 残留 desktop 的代码字节,
       dir_find_free_slot 走错分支 → CREATE 静默失败)。
       恒等映射区 (VA==PA) 已由 mm 预留, 内核可直接写。 */
    {
        u8 *z = (u8 *)USER_LOAD_ADDR;
        for (u32 i = (u32)sz; i < map_sz; i++) z[i] = 0;
    }

    int tpid = task_create_user((void *)USER_LOAD_ADDR, USER_STACK_TOP, user_pd);
    serial_write_str("launch: task pid=");
    serial_write_u32((u32)tpid);
    serial_write_str(" kstack=0x");
    serial_write_u32(current_task ? current_task->kernel_stack : 0);
    serial_write_char('\n');
    if (tpid < 0) serial_write_str("launch: TASK CREATE FAILED\n");
    if (current_task) current_task->state = TASK_RUNNING;

    int ac = args ? count_args(args) : 0;
    enter_user_mode(USER_LOAD_ADDR, USER_STACK_TOP, user_pd, ac, args);

    /* 本任务无父进程 (孤儿): 退出走 task_do_exit 的 orphan 分支跳回此处。
       必须就地回收 — 否则 task_struct / 内核栈 / 页目录全部泄漏, 且
       current_task 悬空指向 DEAD 任务, 下一次 task_create_user 会把
       它当成父进程 (pid != 0) → 新任务退出走父进程分支 → 就绪队列空
       → schedule 落入 hlt 死循环。 */
    task_cleanup_user();
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
    /* 段 + .bss 不得侵入用户栈区 (ELF 的 p_memsz 已含 .bss) */
    if (total_size > USER_STACK_TOP - USER_STACK_SZ - load_base) {
        delete user_pd;
        fail("elf: program too large");
        return -1;
    }
    if (total_size > 0)
        map_user_pages(user_pd, load_base, total_size);
    map_user_pages(user_pd, USER_STACK_TOP - USER_STACK_SZ, USER_STACK_SZ);
    map_user_fb(user_pd);  /* so ring3 can access framebuffer directly */

    task_create_user((void *)entry, USER_STACK_TOP, user_pd);
    if (current_task) current_task->state = TASK_RUNNING;

    int ac = args ? count_args(args) : 0;
    enter_user_mode(entry, USER_STACK_TOP, user_pd, ac, args);

    /* 孤儿任务就地回收 (同 load_flat_binary) */
    task_cleanup_user();
    __asm__ volatile("sti");
    shell_redraw();
    gfx_putc('\n');
    return 0;
}

int load_binary(const char *path, const char *args) {
    return load_elf_binary(path, args);
}
