#pragma once
#include "lib/types.h"

void heap_init(void);
void *kmalloc(u32 size);
void kfree(void *ptr);
