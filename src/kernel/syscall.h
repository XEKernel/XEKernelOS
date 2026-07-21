#pragma once
#include "kernel/isr.h"

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_READ  3
#define SYS_OPEN  4
#define SYS_FREAD 5
#define SYS_SBRK  6
#define SYS_GETCWD 7
#define SYS_TIME   8
#define SYS_GETFB  9
#define SYS_CLOSE  10

#ifdef __cplusplus
extern "C" {
#endif

void syscall_handler(registers_t *r);

#ifdef __cplusplus
}
#endif
