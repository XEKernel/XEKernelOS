#pragma once
#include "lib/types.h"

void isr_register(int vec, void (*handler)(void));
