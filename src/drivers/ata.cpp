#include "drivers/ata.h"
#include "drivers/serial.h"

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

/* 驱动/磁头寄存器: bit7=1, bit6=LBA=1, bit5=1, bit4=DRV, bit3-0=LBA[27:24] */
u8 AtaController::drive_bits(int drv, u32 lba) {
    return (u8)(0xE0 | ((drv & 1) << 4) | ((lba >> 24) & 0x0F));
}

int AtaController::identify_drv(int drv, u16 *buf) {
    outb(ATA_DRIVE, drive_bits(drv, 0));
    outb(ATA_SECTORS, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, ATA_CMD_IDENT);

    u8 st = inb(ATA_STATUS);
    if (st == 0) return -1;          /* 无此驱动器 */

    if (wait(ATA_SR_BSY, 0)) return -2;

    st = inb(ATA_STATUS);
    u8 mid = inb(ATA_LBA_MID);
    u8 hi  = inb(ATA_LBA_HI);
    if (mid == 0x14 && hi == 0xEB) return -3;   /* ATAPI 设备 */

    while (1) {
        st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return -4;
        if (st & ATA_SR_DRQ) break;
    }

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return 0;
}

int AtaController::identify(u16 *buf) {
    return identify_drv(drive_, buf);
}

int AtaController::read_drv(int drv, u32 lba, u8 count, u16 *buf) {
    outb(ATA_DRIVE, drive_bits(drv, lba));
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

int AtaController::read(u32 lba, u8 count, u16 *buf) {
    return read_drv(drive_, lba, count, buf);
}

int AtaController::write(u32 lba, u8 count, const u16 *buf) {
    outb(ATA_DRIVE, drive_bits(drive_, lba));
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

/* 扇区 0 是否像一个 FAT 引导扇区 (BPB 各字段自洽)。
   用于在"启动盘"与"数据盘"之间区分 — 内核镜像的 MBR 引导扇区
   BPB 区域全 0, 判为非法。 */
static bool looks_like_fat(const u8 *b) {
    u16 bps      = *(const u16 *)(b + 11);
    u8  spc      = b[13];
    u16 reserved = *(const u16 *)(b + 14);
    u8  nfats    = b[16];
    u16 root     = *(const u16 *)(b + 17);
    u16 tsz16    = *(const u16 *)(b + 19);
    u16 fsz16    = *(const u16 *)(b + 22);
    u32 tsz32    = *(const u32 *)(b + 32);
    u32 fsz32    = *(const u32 *)(b + 36);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return false;
    if (spc == 0 || (spc & (spc - 1)) != 0) return false;   /* 2 的幂 */
    if (nfats != 1 && nfats != 2) return false;
    if (reserved == 0) return false;
    if (fsz16 == 0 && fsz32 == 0) return false;
    if (tsz16 == 0 && tsz32 == 0) return false;
    (void)root;
    return true;
}

void AtaController::probe_data_drive() {
    u8 sec[512] __attribute__((aligned(4)));
    u16 idbuf[256];
    bool fat[2] = { false, false };

    /* 逐盘探测并记录 (VMware/真实机器排障时靠这几行日志定位) */
    for (int d = 0; d < 2; d++) {
        int ir = identify_drv(d, idbuf);
        serial_write_str("ata: drv");
        serial_write_char('0' + d);
        serial_write_str(" ident=");
        serial_write_char(ir == 0 ? 'K' : 'x');
        if (ir == 0 && read_drv(d, 0, 1, (u16 *)sec) == 0) {
            fat[d] = looks_like_fat(sec);
            serial_write_str(" sec0=");
            serial_write_char((char)sec[0]);
            serial_write_char((char)sec[1]);
            serial_write_char((char)sec[2]);
            serial_write_str(" fat=");
            serial_write_char(fat[d] ? 'Y' : 'n');
        }
        serial_write_char('\n');
    }

    /* 主盘优先 (若它确实带 FAT 卷), 否则沿用从盘默认值 */
    if (fat[0])      drive_ = 0;
    else if (fat[1]) drive_ = 1;
    else             drive_ = 1;   /* 都不可识别: 保持旧行为 (QEMU -hda/-hdb) */
}

/* C-linkage wrappers for Rust FFI */
extern "C" int ata_read_c(u32 lba, u8 count, u16 *buf)  { return ata.read(lba, count, buf); }
extern "C" int ata_write_c(u32 lba, u8 count, const u16 *buf) { return ata.write(lba, count, buf); }
