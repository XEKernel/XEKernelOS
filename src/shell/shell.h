#pragma once
#include "kernel/isr.h"

void shell_loop(void);
void shell_redraw(void);
void shell_launch_user(void);  /* 直接启动用户态 (不返回内核 shell) */
