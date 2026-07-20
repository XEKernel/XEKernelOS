#include "drivers/keyboard.h"
#include "drivers/gfx.h"
#include "lib/ports.h"

static const char kbd_tab[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
};

static void kb_cmd(u8 cmd) {
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x60, cmd);
}

static u8 kb_await(void) {
    for (int i = 0; i < 100000; i++) {
        u8 st = inb(KB_STATUS);
        if (st & 1) return inb(KB_DATA);
    }
    return 0;
}

void kb_init(void) {
    outb(0x64, 0xAE);
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x64, 0x60);
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x60, 0x44);
    while (inb(KB_STATUS) & 1) inb(KB_DATA);

    kb_cmd(0xF4);
    kb_await();
}

static u8 kb_read_scan(void) {
    for (;;) {
        u8 st = inb(KB_STATUS);
        if (st & 1) {
            u8 data = inb(KB_DATA);
            if (st & 0x20) continue;
            return data;
        }
    }
}

char kb_getchar(void) {
    for (;;) {
        u8 s = kb_read_scan();
        if (s & 0x80) continue;
        if (s < sizeof(kbd_tab) && kbd_tab[s])
            return kbd_tab[s];
    }
}

void kb_readline(char *b, int max) {
    int n = 0;
    for (;;) {
        char c = kb_getchar();
        if (c == '\n') { b[n] = 0; gfx_putc('\n'); return; }
        if (c == '\b') { if (n) { n--; gfx_putc('\b'); } continue; }
        if (c >= ' ' && c <= '~' && n < max-1) { b[n++] = c; gfx_putc(c); }
    }
}

void kb_flush(void) {
    while (inb(KB_STATUS) & 1) inb(KB_DATA);
}
