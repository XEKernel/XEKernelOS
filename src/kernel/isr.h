#pragma once
#include "lib/types.h"

struct registers_t {
    u32 edi, esi, ebp, _esp, ebx, edx, ecx, eax;
    u32 vec;
    u32 err_code;
    u32 eip, cs, eflags;
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
