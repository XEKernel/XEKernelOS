#pragma once
#include "lib/types.h"

int  fat_init(void);
int  fat_dir(void);
int  fat_cd(const char *name);
int  fat_mkdir(const char *name);
int  fat_cat(const char *name);
void fat_cwd_str(char *out, int max);

int  fat_read_file_buf(const char *name, u8 *out, u32 max_len);
int  fat_write_file(const char *name, const u8 *data, u32 size);
int  fat_delete_file(const char *name);
int  fat_rmdir(const char *name);
int  fat_rename(const char *old_name, const char *new_name);
u16  fat_find_free_cluster(void);
u16  fat_alloc_cluster(void);
void fat_set_cluster(u16 cluster, u16 value);
