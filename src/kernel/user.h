#pragma once
#include "lib/types.h"

class PagingManager;
void user_init(void);
void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd,
                     int argc, const char *args);

extern PagingManager *g_user_pd;
extern u32 g_entry_esp;
/* User program arguments — stored by loader, consumed by enter_user_mode */
extern char g_user_args[256];
void user_tss_set_esp0(u32 esp0);  /* 更新 TSS ESP0 */

/* 进入 ring3 时是否在 EFLAGS 里开中断 (IF)。
   旧实现硬编码 `orl $0x200` — 永远是开。
   2026-10-05 VMware 排查: guest 侧状态 (GDT 描述符 / 页表 / CR0-CR4 / TSS)
   经插桩证明与 QEMU 逐值相同, 但 iret 进 ring3 后 VM 立刻进 shutdown。
   最后未验证的一条路是「iret 一开 IF, CPU 在第一条用户指令前就投递挂起的
   IRQ0」(load_flat_binary 里 cli 之后有十几毫秒串口打印, PIT 必然把 IRQ0
   挂在 PIC 的 IRR 上)。置 false 可把这条路彻底排除。
   副作用很小: SYS_READ 走 kb_readline 轮询键盘 (不依赖 PIT), 所以
   键盘/鼠标仍可用; 失去的只有时间片抢占与 PIT 驱动的光标刷新。 */
extern bool g_ring3_irq_on;

/* Ring0 兼容模式。
   2026-10-05 VMware 排查结论（docs/VMWARE.md §5.1.4）：
     · `iret` 进 ring3 **成功**（纯 `jmp $` 桩在 VMware 上不再三重故障）；
     · 但 ring3→ring0 的**特权级切换式中断投递**（`int 0x80` 经 IDT 门 +
       TSS 切栈）在 VMware 前端上直接三重故障，且该路径需要的
       IDT 门 / TSS / SS0 / ESP0 目标页映射**全部验证正确**（与 QEMU 逐值相同）。
   本模式把程序放在 ring0 (CS=0x18) 执行 —— `int 0x80` 因此**不发生特权切换**，
   不碰 TSS 栈切换，绕开该故障点，代价是没有内存保护。仅作 VMware 兼容/演示用。
   置 false 即恢复真正的 Ring3 用户态。 */
extern bool g_ring0_mode;
