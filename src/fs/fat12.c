#include "fs/fat12.h"
#include "drivers/ata.h"
#include "drivers/gfx.h"
#include "lib/types.h"

static u8 buf[512];
static int start_sec, fat_secs, root_secs, data_sec;
static u16 spc, bps;

static void name83_to_str(u8 *e, char *name) {
    int n = 0;
    for (int k = 0; k < 8 && e[k] != ' '; k++) name[n++] = e[k];
    name[n++] = '.';
    for (int k = 8; k < 11 && e[k] != ' '; k++) name[n++] = e[k];
    if (name[n-1] == '.') n--;
    name[n] = 0;
}

static int read_root_sec(int sector, u8 *dst) {
    return ata_read(start_sec + fat_secs * 2 + sector, 1, (u16 *)dst);
}

static u16 next_cluster(u16 cl) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    ata_read(start_sec + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1) return val >> 4;
    return val & 0xFFF;
}

int fat_init(void) {
    int r = ata_read(0, 1, (u16 *)buf);
    if (r) return -1;

    bps = *(u16 *)(buf + 11);
    spc = buf[13];
    u16 reserved = *(u16 *)(buf + 14);
    u8  num_fat = buf[16];
    u16 root_ents = *(u16 *)(buf + 17);
    u16 fat_size  = *(u16 *)(buf + 22);

    start_sec = reserved;
    fat_secs  = fat_size;
    root_secs = reserved + num_fat * fat_size;
    data_sec  = root_secs + (root_ents * 32 + bps - 1) / bps;

    return 0;
}

int fat_dir(void) {
    int secs = data_sec - root_secs;
    for (int s = 0; s < secs; s++) {
        if (read_root_sec(s, buf)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return 0;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;

            char name[13];
            name83_to_str(e, name);

            if (e[11] & 0x10) gfx_puts("DIR ");
            else              gfx_puts("    ");

            u32 sz = *(u32 *)(e + 28);
            char ss[10]; int p = 8;
            for (int x = 0; x < 9; x++) ss[x] = ' ';
            u32 t = sz; p = 8;
            if (!t) { ss[8] = '0'; }
            else while (t && p >= 0) { ss[p--] = '0' + (t % 10); t /= 10; }

            gfx_puts(ss); gfx_putc(' ');
            gfx_puts(name); gfx_putc('\n');
        }
    }
    return 0;
}

int fat_cd(const char *name) { (void)name; return 0; }
int fat_mkdir(const char *name) { (void)name; return 0; }

int fat_cat(const char *name) {
    u8 fname[11];
    for (int i = 0; i < 11; i++) fname[i] = ' ';
    int si = 0, di = 0;
    while (name[si] && name[si] != '.') fname[di++] = name[si++];
    if (name[si] == '.') { si++; for (int k = 0; k < 3 && name[si+k]; k++) fname[8+k] = name[si+k]; }
    for (int i = 0; i < 11; i++)
        if (fname[i] >= 'a' && fname[i] <= 'z') fname[i] -= 32;

    int secs = data_sec - root_secs;
    for (int s = 0; s < secs; s++) {
        if (read_root_sec(s, buf)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            u16 cl = *(u16 *)(e + 26);
            u32 size = *(u32 *)(e + 28);
            u8 dbuf[512];
            ata_read(data_sec + (cl - 2) * spc, 1, (u16 *)dbuf);
            int len = (size > spc * bps) ? (int)(spc * bps) : (int)size;
            for (int k = 0; k < len; k++) gfx_putc(dbuf[k]);
            return 0;
        }
    }
    return -1;
}

void fat_cwd_str(char *out, int max) {
    (void)max; out[0] = '\\'; out[1] = 0;
}
