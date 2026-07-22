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

#ifdef __cplusplus
}
#endif
