#pragma once
#include "lib/types.h"

typedef struct {
    u32 edi, esi, ebp, _esp, ebx, edx, ecx, eax;
    u32 vec;
    u32 err_code;
    u32 eip, cs, eflags;
} registers_t;

void isr_register(int vec, void (*handler)(void));
