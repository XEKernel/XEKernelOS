/* 准则五: User-space FAT12 filesystem library
   Uses SYS_DISK_READ/WRITE raw sector reads/writes.
   This proves filesystem logic can live entirely in Ring 3. */

#pragma once

/* Read operations */
int  ufs_init();
int  ufs_ls();               /* list current directory */
int  ufs_cd(const char *name); /* change directory */
int  ufs_cat(const char *name); /* print file content to screen */
int  ufs_read_file(const char *name, char *buf, int max);
int  ufs_size(const char *name); /* returns file size, -1 if not found */
void ufs_cwd(char *out, int max); /* get current directory path */

/* Write operations (准则五) */
int  ufs_create(const char *name, const char *data, int size);
int  ufs_rm(const char *name);    /* delete file */
int  ufs_mkdir(const char *name);
int  ufs_rmdir(const char *name); /* remove empty directory */
int  ufs_mv(const char *old_name, const char *new_name); /* rename */
