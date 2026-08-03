#pragma once
#include "lib/types.h"

/* Block cache — LRU cache of 64 disk sectors to reduce ATA reads.
   Transparently replaces ata_read/ata_write. */

#ifdef __cplusplus
extern "C" {
#endif

int  bc_read(u32 lba, u8 count, void *buf);
int  bc_write(u32 lba, u8 count, const void *buf);
void bc_flush(void);
void bc_init(void);

/* Invalidate cached sectors [lba, lba+count). 用户态驱动 (ufs) 通过
   SYS_DISK_WRITE 直写磁盘时内核必须调用, 否则 bcache 里的旧数据
   会让内核侧 (fat12/ext2) 读到过期内容. */
void bc_invalidate(u32 lba, u8 count);

#ifdef __cplusplus
}
#endif
