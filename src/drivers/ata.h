#pragma once
#include "lib/types.h"

#define ATA_DATA     0x1F0
#define ATA_ERR      0x1F1
#define ATA_SECTORS  0x1F2
#define ATA_LBA_LO   0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HI   0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_STATUS   0x1F7
#define ATA_CMD      0x1F7

#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC

int  ata_identify(u16 *buf);
int  ata_read(u32 lba, u8 count, u16 *buf);
int  ata_write(u32 lba, u8 count, const u16 *buf);
