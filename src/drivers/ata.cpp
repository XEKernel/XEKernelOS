#include "drivers/ata.h"

AtaController ata;

void AtaController::delay_400ns() {
    inb(ATA_STATUS); inb(ATA_STATUS);
    inb(ATA_STATUS); inb(ATA_STATUS);
}

int AtaController::wait(u8 mask, u8 val) {
    for (int timeout = 0; timeout < 100000; timeout++) {
        u8 st = inb(ATA_STATUS);
        if ((st & mask) == val) return 0;
        if ((st & ATA_SR_ERR) || (st & ATA_SR_DF)) return -1;
    }
    return -2;
}

int AtaController::identify(u16 *buf) {
    outb(ATA_DRIVE, 0xA0);
    outb(ATA_SECTORS, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, ATA_CMD_IDENT);

    u8 st = inb(ATA_STATUS);
    if (st == 0) return -1;

    if (wait(ATA_SR_BSY, 0)) return -2;

    st = inb(ATA_STATUS);
    u8 mid = inb(ATA_LBA_MID);
    u8 hi  = inb(ATA_LBA_HI);
    if (mid == 0x14 && hi == 0xEB) return -3;

    while (1) {
        st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return -4;
        if (st & ATA_SR_DRQ) break;
    }

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return 0;
}

int AtaController::read(u32 lba, u8 count, u16 *buf) {
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTORS, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_CMD, ATA_CMD_READ);

    for (int s = 0; s < count; s++) {
        if (wait(ATA_SR_BSY, 0)) return -1;
        if (wait(ATA_SR_DRQ, ATA_SR_DRQ)) return -2;

        for (int i = 0; i < 256; i++)
            buf[s * 256 + i] = inw(ATA_DATA);
    }
    return 0;
}

int AtaController::write(u32 lba, u8 count, const u16 *buf) {
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTORS, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_CMD, ATA_CMD_WRITE);

    for (int s = 0; s < count; s++) {
        if (wait(ATA_SR_BSY, 0)) return -1;
        if (wait(ATA_SR_DRQ, ATA_SR_DRQ)) return -2;

        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, buf[s * 256 + i]);
    }
    return 0;
}
