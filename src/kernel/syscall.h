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
#define SYS_GETPID     27
#define SYS_KILL       28
#define SYS_SIGACTION  29
#define SYS_SIGRETURN  30
#define SYS_STAT       31
#define SYS_LSEEK      32
#define SYS_DUP        33
#define SYS_DUP2       34
#define SYS_PIPE       35
#define SYS_FWRITE     36
#define SYS_SET_OUTFD  37
#define SYS_FSYNC      38
#define SYS_RD_CREATE  39
#define SYS_RD_READ    40
#define SYS_RD_LIST    41
#define SYS_RD_REMOVE  42
#define SYS_DROP_CAP   43

#ifdef __cplusplus
extern "C" {
#endif

void syscall_handler(registers_t *r);

#ifdef __cplusplus
}
#endif
