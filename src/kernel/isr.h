#pragma once
#include "lib/types.h"

struct registers_t {
    u32 edi, esi, ebp, _esp, ebx, edx, ecx, eax;
    u32 vec;
    u32 err_code;
    u32 eip, cs, eflags;
    /* 仅当被中断上下文为 ring3 时, CPU 才在 eflags 之上压入这两个字;
       与 pusha/宏压入的字段连续, 构成完整 iretd 帧。
       ring0 帧上读到的是栈外数据 — 只允许在 (cs&3)==3 时读写。 */
    u32 user_esp, user_ss;
};

class IsrManager {
public:
    void register_handler(int vec, void (*handler)(void));
    void (*lookup(int vec))(void) { return handlers_[vec & 255]; }

private:
    void (*handlers_[256])(void) = {};
};

extern IsrManager isr_mgr;

inline void isr_register(int v, void (*h)(void)) { isr_mgr.register_handler(v, h); }
