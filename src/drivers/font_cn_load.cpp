/* Chinese font — loaded from fixed LBA 2048 on disk */
#include "drivers/font_cn.h"
#include "drivers/ata.h"
#include "lib/heap.h"
#include "drivers/serial.h"

#define FONT_LBA 2048
#define FONT_MAX_SIZE (950 * 1024)

int  font_cn_loaded = 0;
u8  *font_cn_buf = nullptr;
int  font_cn_count_val = 0;

void font_cn_load(void) {
    serial_write_str("font: kmalloc...");
    font_cn_buf = (u8 *)kmalloc(FONT_MAX_SIZE);
    if (!font_cn_buf) { serial_write_str("FAIL\n"); return; }
    serial_write_str("ok, test single sector LBA 2048...");

    /* Test: read just 1 sector at LBA 2048 first */
    int r = ata_read(2048, 1, (u16 *)font_cn_buf);
    serial_write_str("r=");
    serial_write_char('0'+(r/10)%10);
    serial_write_char('0'+r%10);
    serial_write_str(" b0=");
    serial_write_char("0123456789ABCDEF"[(font_cn_buf[0]>>4)&15]);
    serial_write_char("0123456789ABCDEF"[font_cn_buf[0]&15]);
    serial_write_str(" b1=");
    serial_write_char("0123456789ABCDEF"[(font_cn_buf[1]>>4)&15]);
    serial_write_char("0123456789ABCDEF"[font_cn_buf[1]&15]);
    serial_write_str("\n");

    if (r) { kfree(font_cn_buf); font_cn_buf = nullptr; return; }

    /* Read rest in 1-sector chunks */
    int remain = ((FONT_MAX_SIZE + 511) / 512) - 1;
    u32 lba = 2049;
    u8 *ptr = font_cn_buf + 512;
    while (remain > 0) {
        r = ata_read(lba, 1, (u16 *)ptr);
        if (r) { serial_write_str("ERR at "); serial_write_char('0'+(lba/1000)%10); serial_write_char('0'+(lba/100)%10); serial_write_char('0'+(lba/10)%10); serial_write_char('0'+lba%10); serial_write_str("\n"); kfree(font_cn_buf); font_cn_buf = nullptr; return; }
        lba++; ptr += 512; remain--;
    }
    serial_write_str(" all read ok\n");

    font_cn_count_val = font_cn_buf[0] | (font_cn_buf[1] << 8);
    serial_write_str("font: loaded ");
    serial_write_char('0'+(font_cn_count_val/10000)%10);
    serial_write_char('0'+(font_cn_count_val/1000)%10);
    serial_write_char('0'+(font_cn_count_val/100)%10);
    serial_write_char('0'+(font_cn_count_val/10)%10);
    serial_write_char('0'+font_cn_count_val%10);
    serial_write_str(" chars\n");
    font_cn_loaded = 1;
}

const unsigned char *font_cn_lookup_impl(u16 cp) {
    if (!font_cn_loaded) return nullptr;

    int lo = 0, hi = font_cn_count_val - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const GlyphEntry *e = (const GlyphEntry *)(font_cn_buf + 2 + mid * sizeof(GlyphEntry));
        if (e->cp == cp)
            return e->bitmap;
        else if (e->cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}
