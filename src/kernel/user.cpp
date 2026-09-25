#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/ports.h"
#include "drivers/serial.h"
#include "kernel/task.h"

static u32 tss_page;
PagingManager *g_user_pd = nullptr;
u32 g_entry_esp = 0;
char g_user_args[256];

void user_tss_set_esp0(u32 esp0) {
    u8 *tss = (u8 *)tss_page;
    *(u32 *)(tss + 4) = esp0;
}

void user_init(void) {
    tss_page = mm_alloc_page();
    u8 *tss = (u8 *)tss_page;
    for (int i = 0; i < 104; i++) tss[i] = 0;
    *(u32 *)(tss + 4)  = 0x9F000;
    *(u32 *)(tss + 8)  = 0x10;
    /* I/O 位图基址必须 >= TSS 限制(103) 才表示"禁止 ring3 端口 I/O";
       置 0 会被解释成"位图从 TSS 偏移 0 开始", 属于未定义摆法 */
    *(u16 *)(tss + 102) = 104;

    struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    u8 *gdt = (u8 *)gdtr.base;
    int idx = 6;
    u32 base = tss_page;
    gdt[idx*8 + 2] = base & 0xFF;
    gdt[idx*8 + 3] = (base >> 8) & 0xFF;
    gdt[idx*8 + 4] = (base >> 16) & 0xFF;
    gdt[idx*8 + 7] = (base >> 24) & 0xFF;
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
}

void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd,
                     int argc, const char *args) {
    __asm__ volatile("mov %%ebp, %0" : "=m"(g_entry_esp));

    if (args && args[0]) {
        /* Copy args into global buffer (accessible from user CR3) */
        int i = 0;
        while (args[i] && i < 255) { g_user_args[i] = args[i]; i++; }
        g_user_args[i] = 0;
    } else {
        g_user_args[0] = 0;
    }

    if (pd) {
        g_user_pd = pd;
        pd->load();
        __asm__ volatile("wbinvd");
    }

    /* 每任务独立内核栈 */
    if (current_task && current_task->kernel_stack)
        user_tss_set_esp0(current_task->kernel_stack + KSTACK_SIZE);
    else
        serial_write_str("enter_user: WARN no kernel stack (TSS ESP0 stale!\n");

    /* 进 ring3 前的现场记录: 若紧跟其后 triple fault, 这几行就是最后线索 */
    serial_write_str("enter_user: cr3=0x");
    { u32 cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
      serial_write_u32(cr3); }
    serial_write_str(" esp0=0x");
    serial_write_u32(current_task && current_task->kernel_stack
                     ? current_task->kernel_stack + KSTACK_SIZE : 0);
    serial_write_str(" entry=0x");
    serial_write_u32(entry);
    serial_write_str(" ustack=0x");
    serial_write_u32(stack_top);
    serial_write_char('\n');

    if (argc > 0 && g_user_args[0]) {
        /* 把参数字符串本体先拷到用户栈顶下方, 再压 argv 指针数组。
           旧实现把内核 g_user_args 的地址直接作为 argv — 用户页表
           无此映射 (且无 USER 位), 程序访问 argv 必 #PF。
           用户栈为恒等映射 (VA==PA), 内核经 PSE 可直接写。 */
        u32 sp = stack_top;

        /* 1. 自底向上逐字拷贝参数串 (原地分割为 NUL 结尾) */
        u32 str_addrs[16];
        int ac = 0;
        char *argp = g_user_args;
        while (*argp && ac < 16) {
            while (*argp == ' ') argp++;
            if (!*argp) break;
            str_addrs[ac++] = sp;   /* 该串在用户栈上的地址 */
            while (*argp && *argp != ' ')
                *(char *)(sp++) = *argp++;
            *(char *)(sp++) = 0;
        }

        /* 2. 对齐后压 argv 数组 + NULL + argc */
        sp &= ~3u;
        u32 *stk = (u32 *)sp;
        *(--stk) = 0;                       /* argv[ac] = NULL */
        for (int i = ac - 1; i >= 0; i--)
            *(--stk) = str_addrs[i];        /* argv[i] */
        *(--stk) = (u32)ac;                 /* argc */
        if (current_task)
            current_task->user_esp = (u32)stk;   /* 调度恢复时用真实 esp */

        __asm__ volatile(
            "pushl %0\n"   /* SS */
            "pushl %1\n"   /* ESP (argc location) */
            "pushf\n"
            "orl $0x200, (%%esp)\n"
            "pushl $0x2B\n"
            "pushl %2\n"   /* EIP */
            "iret\n"
            :
            : "i"(0x23), "r"((u32)(stk)), "r"(entry)
        );
    } else {
        __asm__ volatile(
            "pushl $0x23\n"
            "pushl %1\n"        /* user stack from stack_top param */
            "pushf\n"
            "orl $0x200, (%%esp)\n"
            "pushl $0x2B\n"
            "pushl %0\n"
            "iret\n"
            :
            : "r"(entry), "r"(stack_top)
        );
    }
    __builtin_unreachable();
}
