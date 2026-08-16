#pragma once
#include "lib/types.h"

void mm_init(void);
u32  mm_alloc_page(void);
void mm_free_page(u32 addr);
u32  mm_free_count(void);
u32  mm_total_pages(void);
/* 将 [start,end) 物理区标为已占用 — 供 mm_init 预留
   内核堆/用户恒等映射区/显存等非 mm 分配的内存 */
void mm_reserve(u32 start, u32 end);
