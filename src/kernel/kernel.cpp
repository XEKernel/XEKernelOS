#include "lib/types.h"
#include "lib/ports.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"
#include "drivers/pit.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "lib/heap.h"
#include "lib/list.h"
#include "kernel/isr.h"
#include "kernel/idt.h"
#include "kernel/mm.h"
#include "kernel/paging.h"
#include "kernel/user.h"
#include "kernel/task.h"
#include "kernel/loader.h"
#include "fs/fat12.h"
#include "fs/ext2.h"
#include "fs/vfs.h"
#include "fs/ramdisk.h"
#include "drivers/font_cn.h"
#include "drivers/bcache.h"
#include "shell/shell.h"

extern u32 _bss_start[], _bss_end[];

__attribute__((unused))
static void idle_task(void *arg) {
    (void)arg;
    for (;;) __asm__ volatile("hlt");
}

__attribute__((section(".text.init")))
extern "C" void kernel_main(void) {
    __asm__("cli");
    kb_flush();

    serial_init();
    serial_write_str("=== XEKernelOS boot ===\n");

    u32 *p = _bss_start;
    while (p < _bss_end) *p++ = 0;

    pic_remap();    serial_write_str("pic_remap ok\n");
    idt_init();     serial_write_str("idt_init ok\n");
    mm_init();      serial_write_str("mm_init ok\n");
    /* heap_init 必须先于 paging_init: paging_init 里 new PagingManager()
       走 operator new → kmalloc — 堆未初始化时 kmalloc 读到零 magic
       返回 0, ctor 在 null this 上跑 (对象本体写进物理页 0),
       kernel_paging 指针为 0 — 后续所有 PD 构造跳过内核 PSE 克隆,
       PG 开启后切 CR3 立即取指 #PF → triple fault (实测定位) */
    heap_init();    serial_write_str("heap_init ok\n");
    paging_init();  serial_write_str("paging_init ok\n");
    gfx_init();     serial_write_str("gfx_init ok\n");
    pit_init();     serial_write_str("pit_init ok\n");
    mouse_init();   serial_write_str("mouse_init ok\n");
    kb_init();      serial_write_str("kb_init ok\n");
    user_init();    serial_write_str("user_init ok\n");

    u32 *vbe = (u32 *)0x500;
    u32 fb = vbe[0], w = vbe[1], h = vbe[2], b = vbe[3], pt = vbe[4];

    serial_write_str("VBE: fb=0x"); {
        for (int i = 28; i >= 0; i -= 4)
            serial_write_char("0123456789ABCDEF"[(fb >> i) & 15]);
    }
    serial_write_str(" w="); serial_write_char('0' + w / 1000 % 10); serial_write_char('0' + w / 100 % 10); serial_write_char('0' + w / 10 % 10); serial_write_char('0' + w % 10);
    serial_write_str(" h="); serial_write_char('0' + h / 1000 % 10); serial_write_char('0' + h / 100 % 10); serial_write_char('0' + h / 10 % 10); serial_write_char('0' + h % 10);
    serial_write_str(" bpp=");
    serial_write_char('0' + b / 10 % 10);
    serial_write_char('0' + b % 10);
    serial_write_str(" pitch=");
    serial_write_char('0' + pt / 1000 % 10);
    serial_write_char('0' + pt / 100 % 10);
    serial_write_char('0' + pt / 10 % 10);
    serial_write_char('0' + pt % 10);
    serial_write_char('\n');

    {
        u32 *test = (u32 *)kmalloc(128);
        if (test) {
            for (int i = 0; i < 32; i++) test[i] = i * 4;
            serial_write_str("heap test: ");
            for (int i = 0; i < 4; i++) {
                serial_write_char('0' + (test[i] / 10) % 10);
                serial_write_char('0' + test[i] % 10);
                serial_write_char(' ');
            }
            serial_write_str("OK\n");
            kfree(test);
        } else {
            serial_write_str("heap test: FAIL\n");
        }
    }

    {
        struct list_head head;
        struct test_item { int val; struct list_head list; } a, b, c;
        list_init(&head);
        a.val = 1; b.val = 2; c.val = 3;
        list_add_tail(&a.list, &head);
        list_add_tail(&b.list, &head);
        list_add_tail(&c.list, &head);
        struct list_head *pos;
        int sum = 0;
        list_for_each(pos, &head)
            sum += container_of(pos, struct test_item, list)->val;
        if (sum == 6) serial_write_str("list test passed\n");
        else           serial_write_str("list test FAIL\n");
    }

    /* Load font BEFORE any screen output — so Chinese text renders correctly */
    serial_write_str("fat_init calling...\n");
    int f = fat_init();
    serial_write_str("fat_init done\n");
    vfs_init();                /* init VFS mount table */
    vfs_mount("/", &fat);      /* mount FAT12 at root */
    bc_init();                 /* init block cache after FS is ready */

    /* ext2: LBA 4096 in disk.img */
    serial_write_str("ext2 init...\n");
    int e = ext2.init(4096);
    if (e == 0) {
        vfs_mount("/ext2", &ext2);
        serial_write_str("ext2 mounted at /ext2\n");
    } else {
        serial_write_str("ext2 init failed\n");
    }

    rd_init();                 /* init ramdisk */
    vfs_mount("/tmp", &ramdisk);/* mount ramdisk at /tmp */
    font_cn_load();
    serial_write_str(font_cn_loaded ? "font_cn loaded\n" : "font_cn NOT loaded\n");

    gfx_clear(0x00);

    /* ---- HEADER ---- */
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts_utf8("XEKernelOS v0.2.0 | x86 保护模式\n");
    gfx_set_fg(COLOR_DGRAY);
    gfx_puts("----------------------------------------\n");

    /* ---- STATUS ---- */
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("[OK] ");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("PIC 重映射 (0x20/0x28)\n");

    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("[OK] ");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("IDT + ISR 分发器\n");

    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("[OK] ");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("PIT 100Hz | 键盘中断 | 鼠标中断\n");

    u32 free = mm_free_count();
    char s[16];
    int n = 0, t = free;
    if (t == 0) { s[0] = '0'; n = 1; }
    else { while (t) { s[n++] = '0' + (t % 10); t /= 10; } }
    for (int i = 0; i < n/2; i++) { char c = s[i]; s[i] = s[n-1-i]; s[n-1-i] = c; }
    s[n] = 0;
    gfx_set_fg(COLOR_LGREEN);
    gfx_puts("[OK] ");
    gfx_set_fg(COLOR_LGRAY);
    gfx_puts_utf8("内存初始化: "); gfx_puts(s); gfx_puts_utf8(" 页\n");

    u16 ident[256];
    int r = ata_identify(ident);
    if (!r) {
        char model[41];
        for (int i = 0; i < 20; i++) {
            u16 w = ident[27 + i];
            model[i*2]     = (w >> 8) & 0xFF;
            model[i*2 + 1] = w & 0xFF;
        }
        model[40] = 0;
        for (int i = 39; i > 0 && model[i] == ' '; i--) model[i] = 0;
        gfx_set_fg(COLOR_LGREEN);
        gfx_puts("[OK] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts("ATA: "); gfx_puts(model); gfx_putc('\n');
    } else {
        gfx_set_fg(COLOR_DGRAY);
        gfx_puts("[--] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts_utf8("ATA: 无磁盘\n");
    }

    gfx_set_fg(COLOR_DGRAY);
    gfx_puts("----------------------------------------\n");
    serial_write_str("=== Shell started ===\n");

    if (f == 0) {
        gfx_set_fg(COLOR_LGREEN);
        gfx_puts("[OK] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts_utf8("FAT12 文件系统就绪\n");
    } else {
        gfx_set_fg(COLOR_DGRAY);
        gfx_puts("[--] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts_utf8("FAT12: 无文件系统\n");
    }

    if (e == 0) {
        gfx_set_fg(COLOR_LGREEN);
        gfx_puts("[OK] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts_utf8("ext2 文件系统就绪 (/ext2)\n");
    } else {
        gfx_set_fg(COLOR_DGRAY);
        gfx_puts("[--] ");
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts_utf8("ext2: 未检测到\n");
    }

    task_init();
    serial_write_str("tasks ready\n");

    /* 默认启动 GUI 桌面; 桌面退出 (ESC) 或加载失败后进入用户 Shell。
       内核 Shell 仅应急恢复 */
    gfx_set_fg(COLOR_LCYAN);
    gfx_puts_utf8("\n启动用户态...\n");
    gfx_set_fg(COLOR_LGRAY);

    bool gui_started = false;
    for (int crash_count = 0; ; crash_count++) {
        /* 记录启动时刻 — 正常运行的 Shell 退出后重置计数,
           旧实现只累计重启次数, 第 6 次正常 EXIT 会误报"连续崩溃" */
        u32 launch_tick = pit.ticks();

        if (!gui_started && load_binary("DESKTOP.BIN", nullptr) == 0) {
            /* 桌面正常跑过一轮 (含 ESC 退出) — 后续回 Shell,
               Shell 里可随时 RUN DESKTOP.BIN 再进 */
            gui_started = true;
        } else {
            shell_launch_user();
        }

        if (pit.ticks() - launch_tick > 500)   /* 运行超过 ~5 秒视为正常 */
            crash_count = 0;

        /* 连续崩溃超过 5 次 → 进入内核应急 Shell */
        if (crash_count >= 5) {
            gfx_set_fg(COLOR_LRED);
            gfx_puts_utf8("\n!!! 用户态连续崩溃, 进入内核应急 Shell !!!\n");
            gfx_puts_utf8("输入 USERSH 重试, HELP 查看命令\n\n");
            gfx_set_fg(COLOR_LGRAY);
            shell_loop();
            crash_count = 0;
        }
    }
}
