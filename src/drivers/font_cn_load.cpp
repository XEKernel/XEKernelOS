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
    serial_write_str("ok, ata_read...");

    /* ata_read count is u8 (max 255). Read in 255-sector chunks. */
    int sectors = (FONT_MAX_SIZE + 511) / 512;
    int remain = sectors;
    u32 lba = FONT_LBA;
    u8 *ptr = font_cn_buf;
    while (remain > 0) {
        u8 chunk = (remain > 255) ? 255 : (u8)remain;
        int r = ata_read(lba, chunk, (u16 *)ptr);
        if (r) { serial_write_str("ERR\n"); kfree(font_cn_buf); font_cn_buf = nullptr; return; }
        lba += chunk;
        ptr += chunk * 512;
        remain -= chunk;
    }
    serial_write_str("ok\n");

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
