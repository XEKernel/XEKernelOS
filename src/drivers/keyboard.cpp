#include "drivers/keyboard.h"
#include "drivers/gfx.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "kernel/isr.h"

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

/* IRQ1 handler — 中断驱动键盘输入.
   修复: 原实现轮询 inb(0x64), 但 PS/2 控制器只有 16 字节 FIFO,
   快速打字 (或自动化注入) 会溢出丢键. 中断 handler 在字节到达时
   立刻取走并放入 256 字节软件环形缓冲, 彻底消除 FIFO 溢出. */
void Keyboard::irq_handler() {
    u8 st = inb(KB_STATUS);
    if (st & 1) {  /* output buffer full */
        u8 data = inb(KB_DATA);
        if (st & 0x20) {
            mouse.feed_byte(data);   /* mouse byte → mouse driver */
        } else {
            kb.kb_put(data);         /* keyboard scan code → ring buffer */
        }
    }
}

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
    /* PS/2 配置字节: bit0=IRQ1 (键盘中断) 必须 = 1; bit1=IRQ12 (mouse);
       bit6=scancode set 1 翻译. 修复: 之前用 0x44 (bit0=0) 禁用了 IRQ1,
       是 keyboard 中断驱动完全不工作的根因. */
    outb(0x60, 0x45);
    while (inb(KB_STATUS) & 1) inb(KB_DATA);

    cmd(0xF4);
    await();
    while (inb(KB_STATUS) & 1) inb(KB_DATA);
    shift_ = 0;
    caps_  = 0;
    ctrl_  = 0;
    kb_head_ = 0;
    kb_tail_ = 0;

    /* IRQ1 已由 pic_remap unmask + 配置字节 0x45 启用 — 注册中断处理器
       (QEMU 11.x sendkey 不触发 IRQ1, 真实硬件会触发; read_scan 同时
        主动 drain FIFO 兼容两种情况) */
    isr_register(0x21, &Keyboard::irq_handler);
}

u8 Keyboard::read_scan() {
    int spin = 0;
    for (;;) {
        /* 主动 drain PS/2 硬件 FIFO (修复: 原版只读 1 字节时多字节 FIFO
           会溢出丢键) — QEMU 11.x sendkey 不触发 IRQ1, 故完全靠轮询. */
        u8 st = inb(KB_STATUS);
        while (st & 1) {
            u8 data = inb(KB_DATA);
            if (st & 0x20) mouse.feed_byte(data);
            else            kb.kb_put(data);
            st = inb(KB_STATUS);
        }
        /* 从软件环形缓冲返回 (IRQ1 中断填入的真实硬件数据) */
        if (kb_head_ != kb_tail_) return kb_get();
        /* 无输入时空转更新鼠标光标 — PIT 在 syscall 内 (int 0x80 门清 IF)
           不触发, 因此这里需要手动刷新. ~50000 次迭代 ≈ 5ms 一次 */
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
    /* IRQ1 中断已把字节放入软件缓冲; 这里 peek 缓冲判断 Ctrl+C.
       普通字符留在缓冲等 read_scan 消费, Ctrl/Ctrl+C 键字节被消费. */
    if (kb_head_ == kb_tail_) return 0;   /* 缓冲空 */

    u8 data = kb_buf_[kb_tail_];          /* peek 不消费 */
    if (data == 0x1D)      { ctrl_ = 1; kb_tail_ = (kb_tail_ + 1) % KB_BUF_SIZE; return 0; }
    if (data == 0x9D)      { ctrl_ = 0; kb_tail_ = (kb_tail_ + 1) % KB_BUF_SIZE; return 0; }
    if (data == 0x2E && ctrl_) { kb_tail_ = (kb_tail_ + 1) % KB_BUF_SIZE; return 1; }
    return 0;
}
