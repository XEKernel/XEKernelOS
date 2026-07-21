#pragma once
#include "kernel/isr.h"

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_READ  3
#define SYS_OPEN  4
#define SYS_FREAD 5

#ifdef __cplusplus
extern "C" {
#endif

void syscall_handler(registers_t *r);

#ifdef __cplusplus
}
#endif
