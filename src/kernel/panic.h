#pragma once
#include "kernel/isr.h"

void kernel_panic(registers_t *regs, const char *msg);

#define assert(expr) do { \
    if (!(expr)) kernel_panic(0, "Assertion failed: " #expr); \
} while(0)
