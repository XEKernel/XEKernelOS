#pragma once
#include "lib/types.h"

int  fat_init(void);
int  fat_dir(void);
int  fat_cd(const char *name);
int  fat_mkdir(const char *name);
int  fat_cat(const char *name);
void fat_cwd_str(char *out, int max);
