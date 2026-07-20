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
#include "fs/fat12.h"
#include "shell/shell.h"

extern u32 _bss_start[], _bss_end[];

static void idle_task(void *arg) {
    (void)arg;
    for (;;) __asm__ volatile("hlt");
}

__attribute__((section(".text.init")))
void kernel_main(void) {
    __asm__("cli");
    kb_flush();

    serial_init();
    serial_write_str("=== XEKernelOS boot ===\n");

    u32 *p = _bss_start;
    while (p < _bss_end) *p++ = 0;

    pic_remap();    serial_write_str("pic_remap ok\n");
    idt_init();     serial_write_str("idt_init ok\n");
    mm_init();      serial_write_str("mm_init ok\n");
    gfx_init();     serial_write_str("gfx_init ok\n");
    heap_init();    serial_write_str("heap_init ok\n");
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
    serial_write_str(" bpp="); serial_write_char('0' + b % 10);
    serial_write_str(" pitch="); serial_write_char('0' + pt / 1000 % 10); serial_write_char('0' + pt / 100 % 10);
    serial_write_char('\n');

    {
        u32 *test = kmalloc(128);
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

#if 0
    __asm__ volatile("int3");
    *(volatile int *)0x12345678 = 0x42;
#endif

    gfx_clear(0x00);
    gfx_puts("XEKernelOS v0.2.0 | x86 Protected Mode\n");
    gfx_puts("----------------------------------------\n");

    gfx_puts("[OK] PIC remapped (0x20/0x28)\n");
    gfx_puts("[OK] IDT + ISR dispatcher\n");
    gfx_puts("[OK] PIT 100Hz | Keyboard IRQ | Mouse IRQ\n");

    u32 free = mm_free_count();
    char s[16];
    int n = 0, t = free;
    if (t == 0) { s[0] = '0'; n = 1; }
    else { while (t) { s[n++] = '0' + (t % 10); t /= 10; } }
    for (int i = 0; i < n/2; i++) { char c = s[i]; s[i] = s[n-1-i]; s[n-1-i] = c; }
    s[n] = 0;
    gfx_puts("[OK] MM  init: "); gfx_puts(s); gfx_puts(" pages\n");

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
        gfx_puts("[OK] ATA: "); gfx_puts(model); gfx_putc('\n');
    } else {
        gfx_puts("[--] ATA: no drive\n");
    }

    gfx_puts("----------------------------------------\n");
    serial_write_str("=== Shell started ===\n");

    serial_write_str("fat_init calling...\n");
    int f = fat_init();
    serial_write_str("fat_init done\n");
    if (f == 0) gfx_puts("[OK] FAT12 filesystem ready\n");
    else        gfx_puts("[--] FAT12: no filesystem\n");

    task_init();
    serial_write_str("tasks ready\n");
    shell_loop();
}
