#include "drivers/keyboard.h"
#include "drivers/gfx.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"

const char Keyboard::kbd_low_[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
};

const char Keyboard::kbd_up_[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,
};

Keyboard kb;

void Keyboard::cmd(u8 cmd) {
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x60, cmd);
}

u8 Keyboard::await() {
    for (int i = 0; i < 100000; i++) {
        u8 st = inb(KB_STATUS);
        if (st & 1) return inb(KB_DATA);
    }
    return 0;
}

void Keyboard::kb_put(u8 sc) {
    int next = (kb_head_ + 1) % KB_BUF_SIZE;
    if (next != kb_tail_) { kb_buf_[kb_head_] = sc; kb_head_ = next; }
}

u8 Keyboard::kb_get() {
    while (kb_head_ == kb_tail_) { /* busy-wait */ }
    u8 sc = kb_buf_[kb_tail_];
    kb_tail_ = (kb_tail_ + 1) % KB_BUF_SIZE;
    return sc;
}

void Keyboard::init() {
    outb(0x64, 0xAE);
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x64, 0x60);
    for (int i = 0; i < 10000; i++) if (!(inb(0x64) & 2)) break;
    outb(0x60, 0x44);
    while (inb(KB_STATUS) & 1) inb(KB_DATA);

    cmd(0xF4);
    await();
    while (inb(KB_STATUS) & 1) inb(KB_DATA);
    shift_ = 0;
    caps_  = 0;
    kb_head_ = 0;
    kb_tail_ = 0;
}

u8 Keyboard::read_scan() {
    int spin = 0;
    for (;;) {
        /* Check ring buffer for bytes saved by PIT's ctrl_c */
        if (kb_head_ != kb_tail_) return kb_get();
        u8 st = inb(KB_STATUS);
        if (st & 1) {
            u8 data = inb(KB_DATA);
            if (st & 0x20) { mouse.feed_byte(data); continue; }
            return data;
        }
        /* Update mouse cursor by polling — PIT is blocked during
           syscalls (int 0x80 gate clears IF), so mcursor_update()
           never fires from ISR. Poll every ~50000 iterations (~5ms). */
        if (++spin > 50000) {
            spin = 0;
            gfx.mcursor_update();
        }
    }
}

char Keyboard::getchar() {
    for (;;) {
        u8 s = read_scan();
        if (s == SC_LSHIFT || s == SC_RSHIFT) { shift_ = 1; continue; }
        if (s == (SC_LSHIFT | 0x80) || s == (SC_RSHIFT | 0x80)) { shift_ = 0; continue; }
        if (s == SC_CAPS) { caps_ = !caps_; continue; }
        if (s & 0x80) continue;
        if (s >= (u8)sizeof(kbd_low_)) continue;
        if (s <= 1) continue;
        int shifted = shift_ ^ caps_;
        char c = shifted ? kbd_up_[s] : kbd_low_[s];
        /* CapsLock only inverts case for letters; for punctuation
           and other keys it has no effect. */
        if (caps_ && c) {
            char lo = kbd_low_[s];
            if (!((lo >= 'a' && lo <= 'z') || (lo >= 'A' && lo <= 'Z')))
                c = shift_ ? kbd_up_[s] : kbd_low_[s];
        }
        if (c) return c;
    }
}

void Keyboard::readline(char *b, int max) {
    int n = 0;
    for (;;) {
        char c = getchar();
        if (c == '\n') { b[n] = 0; gfx.putc('\n'); return; }
        if (c == '\b') { if (n) { n--; gfx.putc('\b'); } continue; }
        if (c >= ' ' && c <= '~' && n < max-1) { b[n++] = c; gfx.putc(c); }
    }
}

void Keyboard::flush() {
    while (inb(KB_STATUS) & 1) inb(KB_DATA);
}

int Keyboard::ctrl_c() {
    u8 st = inb(KB_STATUS);
    if (!(st & 1)) return 0;
    u8 data = inb(KB_DATA);

    /* Mouse data — forward to mouse driver, don't treat as keyboard */
    if (st & 0x20) { mouse.feed_byte(data); return 0; }

    if (data == 0x1D)      { ctrl_ = 1; return 0; }
    if (data == 0x9D)      { ctrl_ = 0; return 0; }
    if (data == 0x2E && ctrl_) return 1;

    /* Not a Ctrl key — save the stolen byte in ring buffer */
    kb_put(data);
    return 0;
}
