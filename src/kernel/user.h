#pragma once
#include "lib/types.h"

class PagingManager;
void user_init(void);
void enter_user_mode(u32 entry, u32 stack_top, PagingManager *pd,
                     int argc, const char *args);

extern PagingManager *g_user_pd;
extern u32 g_entry_esp;
/* User program arguments — stored by loader, consumed by enter_user_mode */
extern char g_user_args[256];
