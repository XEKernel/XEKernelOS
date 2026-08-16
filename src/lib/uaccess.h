#pragma once
#include "lib/types.h"

/* 用户态内存访问助手 — 经当前任务页表把用户 VA 翻译为物理地址,
   再经内核 PSE 恒等映射访问。修复: fork/exec 后用户页不再恒等映射,
   内核直接解引用用户指针会读/写错物理页。
   逐 4KB 页翻译, 自动处理跨页; 要求页带 USER 位 (防内核区伪造指针)。
   返回 false = 有页未映射, 调用方应向用户返回 -1。 */
bool copy_from_user(void *kdst, u32 usrc, u32 len);
bool copy_to_user(u32 udst, const void *ksrc, u32 len);
bool copy_str_from_user(char *kdst, u32 usrc, u32 max);  /* 含 NUL, 至多 max 字节 */
