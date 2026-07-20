#pragma once
#include "lib/types.h"
#include "lib/ports.h"

#define ATA_DATA     0x1F0
#define ATA_SECTORS  0x1F2
#define ATA_LBA_LO   0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HI   0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_STATUS   0x1F7
#define ATA_CMD      0x1F7

#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DF    0x20
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC

class AtaController {
public:
    int  identify(u16 *buf);
    int  read(u32 lba, u8 count, u16 *buf);
    int  write(u32 lba, u8 count, const u16 *buf);

private:
    static void delay_400ns();
    static int  wait(u8 mask, u8 val);
};

extern AtaController ata;

inline int ata_identify(u16 *b)            { return ata.identify(b); }
inline int ata_read(u32 l, u8 c, u16 *b)  { return ata.read(l, c, b); }
inline int ata_write(u32 l, u8 c, const u16 *b) { return ata.write(l, c, b); }
