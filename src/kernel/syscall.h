#pragma once
#include "kernel/isr.h"

#define SYS_WRITE 1

void syscall_handler(registers_t *r);
