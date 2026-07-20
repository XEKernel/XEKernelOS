#pragma once
#include "lib/types.h"

void mm_init(void);
u32  mm_alloc_page(void);
void mm_free_page(u32 addr);
u32  mm_free_count(void);
