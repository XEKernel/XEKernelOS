#pragma once
#include "lib/types.h"

class PagingManager;
void user_init(void);
void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd);

/* Set by enter_user_mode, read by ISR for CR3 restore */
extern PagingManager *g_user_pd;
/* Kernel ESP before entering user mode — restored by SYS_EXIT */
extern u32 g_entry_esp;
