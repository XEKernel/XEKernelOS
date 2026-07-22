/* 准则五: User-space FAT12 filesystem library
   Uses SYS_DISK_READ raw sector reads instead of kernel FAT12 syscalls.
   This proves filesystem logic can live entirely in Ring 3. */

#pragma once

/* User-space FAT12 reader — read-only for now */
int  ufs_init();
int  ufs_ls();               /* list current directory */
int  ufs_cd(const char *name); /* change directory */
int  ufs_cat(const char *name); /* print file content to serial */
int  ufs_read_file(const char *name, char *buf, int max);
int  ufs_size(const char *name); /* returns file size, -1 if not found */
