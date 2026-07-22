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
#define SYS_MOUSE  11
#define SYS_SLEEP  12
#define SYS_CLS    13
#define SYS_GFX_PUTC  14
#define SYS_GFX_PUTS  15
#define SYS_GFX_SET_FG 16
#define SYS_FAT_DIR    17
#define SYS_FAT_CD     18
#define SYS_FAT_MKDIR  19
#define SYS_FAT_RMDIR  20
#define SYS_FAT_DELETE 21
#define SYS_FAT_RENAME 22
#define SYS_FAT_WRITE  23
#define SYS_FORK       24
#define SYS_EXEC       25
#define SYS_WAITPID    26

#ifdef __cplusplus
extern "C" {
#endif

void syscall_handler(registers_t *r);

#ifdef __cplusplus
}
#endif
