#pragma once
#include "kernel/isr.h"

void shell_loop(void);
void shell_save_esp(void);
void shell_recover(registers_t *r);
