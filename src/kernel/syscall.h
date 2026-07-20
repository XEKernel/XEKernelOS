#pragma once
#include "kernel/isr.h"

#define SYS_WRITE 1

#ifdef __cplusplus
extern "C" {
#endif

void syscall_handler(registers_t *r);

#ifdef __cplusplus
}
#endif
