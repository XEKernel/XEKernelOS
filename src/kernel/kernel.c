#include "lib/types.h"
#include "lib/ports.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/ata.h"
#include "drivers/pit.h"
#include "drivers/mouse.h"
#include "kernel/isr.h"
#include "kernel/idt.h"
#include "kernel/mm.h"
#include "kernel/paging.h"
#include "kernel/user.h"
#include "fs/fat12.h"
#include "shell/shell.h"

extern u32 _bss_start[], _bss_end[];

static void dbg_serial_init(void) {
    outb(0x3F8 + 1, 0);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F8 + 1, 0);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
}

static void dbg_putc(char c) {
    for (int i = 0; i < 30000; i++)
        if (inb(0x3F8 + 5) & 0x20) break;
    outb(0x3F8, c);
}

static void dbg_puts(const char *s) {
    while (*s) dbg_putc(*s++);
}

__attribute__((section(".text.init")))
void kernel_main(void) {
    __asm__("cli");
    kb_flush();

    dbg_serial_init();
    dbg_puts("=== XEKernelOS boot ===\n");

    u32 *p = _bss_start;
    while (p < _bss_end) *p++ = 0;

    pic_remap();    dbg_puts("pic_remap ok\n");
    idt_init();     dbg_puts("idt_init ok\n");
    mm_init();      dbg_puts("mm_init ok\n");
    gfx_init();     dbg_puts("gfx_init ok\n");
    pit_init();     dbg_puts("pit_init ok\n");
    mouse_init();   dbg_puts("mouse_init ok\n");
    kb_init();      dbg_puts("kb_init ok\n");

    u32 *vbe = (u32 *)0x500;
    u32 fb = vbe[0], w = vbe[1], h = vbe[2], b = vbe[3], pt = vbe[4];

    dbg_puts("VBE: fb=0x"); {
        for (int i = 28; i >= 0; i -= 4)
            dbg_putc("0123456789ABCDEF"[(fb >> i) & 15]);
    }
    dbg_puts(" w="); dbg_putc('0' + w / 1000 % 10); dbg_putc('0' + w / 100 % 10); dbg_putc('0' + w / 10 % 10); dbg_putc('0' + w % 10);
    dbg_puts(" h="); dbg_putc('0' + h / 1000 % 10); dbg_putc('0' + h / 100 % 10); dbg_putc('0' + h / 10 % 10); dbg_putc('0' + h % 10);
    dbg_puts(" bpp="); dbg_putc('0' + b % 10);
    dbg_puts(" pitch="); dbg_putc('0' + pt / 1000 % 10); dbg_putc('0' + pt / 100 % 10);
    dbg_putc('\n');

    gfx_clear(0x01);
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

    dbg_puts("fat_init calling...\n");
    int f = fat_init();
    dbg_puts("fat_init done\n");
    if (f == 0) gfx_puts("[OK] FAT12 filesystem ready\n");
    else                 gfx_puts("[--] FAT12: no filesystem\n");

    gfx_puts("----------------------------------------\n");
    dbg_puts("=== Shell started ===\n");
    shell_loop();
}
