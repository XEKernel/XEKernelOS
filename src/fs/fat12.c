#include "fs/fat12.h"
#include "drivers/ata.h"
#include "drivers/gfx.h"
#include "lib/types.h"

static u8 buf[512];
static int start_sec, fat_secs, root_secs, data_sec;
static u16 spc, bps;
static u8  num_fat;
static u16 root_ents;
static u16 cur_dir_cluster;   /* 0 = root directory */
static char cur_dir_name[13];  /* human-readable name of current dir */

/* ---- forward declarations ---- */
static int read_root_sec(int sector, u8 *dst);
static int write_root_sec(int sector, u8 *src);
static u16 next_cluster(u16 cl);

/* ---- current directory helpers ---- */

static int curdir_secs(void) {
    if (cur_dir_cluster == 0)
        return data_sec - root_secs;
    int n = 0;
    u16 cl = cur_dir_cluster;
    while (cl >= 2 && cl < 0xFF0) { n += spc; cl = next_cluster(cl); }
    return n;
}

static int read_curdir(int sec_idx) {
    if (cur_dir_cluster == 0)
        return read_root_sec(sec_idx, buf);
    u16 cl = cur_dir_cluster;
    int per = spc;
    while (sec_idx >= per) {
        u16 nx = next_cluster(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx; sec_idx -= per;
    }
    return ata_read(data_sec + (cl - 2) * spc + sec_idx, 1, (u16 *)buf);
}

static int write_curdir(int sec_idx) {
    if (cur_dir_cluster == 0)
        return write_root_sec(sec_idx, buf);
    u16 cl = cur_dir_cluster;
    int per = spc;
    while (sec_idx >= per) {
        u16 nx = next_cluster(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx; sec_idx -= per;
    }
    return ata_write(data_sec + (cl - 2) * spc + sec_idx, 1, (u16 *)buf);
}

static void name83_to_str(u8 *e, char *name) {
    int n = 0;
    for (int k = 0; k < 8 && e[k] != ' '; k++) name[n++] = e[k];
    name[n++] = '.';
    for (int k = 8; k < 11 && e[k] != ' '; k++) name[n++] = e[k];
    if (name[n-1] == '.') n--;
    name[n] = 0;
}

static void str_to_name83(const char *name, u8 *fname) {
    for (int i = 0; i < 11; i++) fname[i] = ' ';
    int si = 0, di = 0;
    while (name[si] && name[si] != '.' && di < 8) {
        char c = name[si++];
        if (c >= 'a' && c <= 'z') c -= 32;
        fname[di++] = c;
    }
    if (name[si] == '.') {
        si++;
        for (int k = 0; k < 3 && name[si+k]; k++) {
            char c = name[si+k];
            if (c >= 'a' && c <= 'z') c -= 32;
            fname[8+k] = c;
        }
    }
}

static int read_root_sec(int sector, u8 *dst) {
    return ata_read(root_secs + sector, 1, (u16 *)dst);
}

static int write_root_sec(int sector, u8 *src) {
    return ata_write(root_secs + sector, 1, (u16 *)src);
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

static u16 read_fat_entry(u16 cl) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    ata_read(start_sec + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1) return val >> 4;
    return val & 0xFFF;
}

static void write_fat_entry(u16 cl, u16 value) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    ata_read(start_sec + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1)
        val = (val & 0x000F) | ((value & 0xFFF) << 4);
    else
        val = (val & 0xF000) | (value & 0xFFF);
    *(u16 *)(fbuf + fat_pos) = val;
    ata_write(start_sec + fat_sec, 1, (u16 *)fbuf);
    for (int i = 1; i < num_fat; i++)
        ata_write(start_sec + i * fat_secs + fat_sec, 1, (u16 *)fbuf);
}

int fat_init(void) {
    int r = ata_read(0, 1, (u16 *)buf);
    if (r) return -1;
    bps = *(u16 *)(buf + 11);
    spc = buf[13];
    u16 reserved = *(u16 *)(buf + 14);
    num_fat = buf[16];
    root_ents = *(u16 *)(buf + 17);
    u16 fat_size  = *(u16 *)(buf + 22);
    start_sec = reserved;
    fat_secs  = fat_size;
    root_secs = reserved + num_fat * fat_size;
    data_sec  = root_secs + (root_ents * 32 + bps - 1) / bps;
    return 0;
}

u16 fat_find_free_cluster(void) {
    u16 max_cluster = (fat_secs * 512 * 2) / 3;
    for (u16 cl = 2; cl < max_cluster; cl++)
        if (read_fat_entry(cl) == 0) return cl;
    return 0;
}

u16 fat_alloc_cluster(void) {
    u16 cl = fat_find_free_cluster();
    if (!cl) return 0;
    write_fat_entry(cl, 0xFFF);
    return cl;
}

void fat_set_cluster(u16 cluster, u16 value) {
    write_fat_entry(cluster, value);
}

int fat_read_file_buf(const char *name, u8 *out, u32 max_len) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (e[11] & 0x10) continue;  /* skip directories */
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            u16 cl = *(u16 *)(e + 26);
            u32 size = *(u32 *)(e + 28);
            u32 remain = (size < max_len) ? size : max_len;
            u32 offset = 0;
            while (cl >= 2 && cl < 0xFF0 && remain) {
                u32 chunk = spc * bps;
                if (chunk > remain) chunk = remain;
                ata_read(data_sec + (cl - 2) * spc, (chunk + bps - 1) / bps, (u16 *)(out + offset));
                offset += chunk;
                remain -= chunk;
                cl = next_cluster(cl);
            }
            return (int)size;
        }
    }
    return -1;
}

int fat_write_file(const char *name, const u8 *data, u32 size) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    int free_entry = -1;
    int found = 0;

    for (int s = 0; s < maxs && !found; s++) {
        if (read_curdir(s)) return -3;
        for (int j = 0; j < 512 && !found; j += 32) {
            u8 *e = buf + j;
            if (free_entry < 0 && (e[0] == 0xE5 || e[0] == 0)) free_entry = s * 512 + j;
            if (e[0] == 0) { found = 1; break; }
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != fname[k]) { match = 0; break; }
            if (match) {
                u16 cl = *(u16 *)(e + 26);
                while (cl >= 2 && cl < 0xFF0) {
                    u16 next = next_cluster(cl);
                    write_fat_entry(cl, 0);
                    cl = next;
                }
                free_entry = s * 512 + j;
                found = 1;
            }
        }
    }

    if (free_entry < 0) return -4;

    u32 needed_clusters = (size + spc * bps - 1) / (spc * bps);
    u16 first_cl = 0, prev_cl = 0;
    for (u32 i = 0; i < needed_clusters; i++) {
        u16 cl = fat_alloc_cluster();
        if (!cl) return -5;
        if (!first_cl) first_cl = cl;
        if (prev_cl) write_fat_entry(prev_cl, cl);
        prev_cl = cl;
    }

    u32 written = 0;
    u16 cl = first_cl;
    while (cl >= 2 && cl < 0xFF0 && written < size) {
        u32 chunk = spc * bps;
        if (chunk > size - written) chunk = size - written;
        u32 sec_count = (chunk + bps - 1) / bps;
        if (sec_count > 0)
            ata_write(data_sec + (cl - 2) * spc, sec_count, (u16 *)(data + written));
        written += chunk;
        cl = next_cluster(cl);
    }

    int sec_idx = free_entry / 512;
    int entry_off = free_entry % 512;
    if (read_curdir(sec_idx)) return -3;
    u8 *entry = buf + entry_off;
    for (int k = 0; k < 11; k++) entry[k] = fname[k];
    entry[11] = 0x20;
    *(u16 *)(entry + 26) = first_cl;
    *(u32 *)(entry + 28) = size;
    write_curdir(sec_idx);
    return 0;
}

int fat_delete_file(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) return -1;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            u16 cl = *(u16 *)(e + 26);
            while (cl >= 2 && cl < 0xFF0) {
                u16 next = next_cluster(cl);
                write_fat_entry(cl, 0);
                cl = next;
            }
            e[0] = 0xE5;
            write_curdir(s);
            return 0;
        }
    }
    return -1;
}

int fat_dir(void) {
    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return 0;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            char name[13];
            name83_to_str(e, name);
            if (e[11] & 0x10) {
                gfx_set_fg(COLOR_LCYAN);
                gfx_puts("DIR ");
            } else {
                gfx_set_fg(COLOR_LGRAY);
                gfx_puts("    ");
            }
            u32 sz = *(u32 *)(e + 28);
            char ss[10]; int p = 8;
            for (int x = 0; x < 9; x++) ss[x] = ' ';
            u32 t = sz; p = 8;
            if (!t) { ss[8] = '0'; }
            else while (t && p >= 0) { ss[p--] = '0' + (t % 10); t /= 10; }
            gfx_set_fg(COLOR_DGRAY);
            gfx_puts(ss); gfx_putc(' ');
            gfx_set_fg(COLOR_WHITE);
            gfx_puts(name); gfx_putc('\n');
            gfx_set_fg(COLOR_LGRAY);
        }
    }
    return 0;
}

int fat_cd(const char *name) {
    /* cd \  →  root */
    if (*name == '\\' && name[1] == 0) {
        cur_dir_cluster = 0;
        cur_dir_name[0] = 0;
        return 0;
    }
    while (*name == ' ') name++;
    if (!*name) return -1;

    /* cd ..  →  parent */
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        if (cur_dir_cluster == 0) return 0;  /* already root */
        /* read ".." entry from first sector of current dir */
        if (read_curdir(0)) return -1;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) break;
            if (e[0] == 0xE5) continue;
            if (!(e[11] & 0x10)) continue;
            if (e[0] == '.' && e[1] == '.' && e[2] == ' ') {
                cur_dir_cluster = *(u16 *)(e + 26);
                cur_dir_name[0] = cur_dir_cluster ? '?' : 0;
                cur_dir_name[1] = 0;
                return 0;
            }
        }
        return -1;
    }

    /* cd .  →  no-op */
    if (name[0] == '.' && name[1] == 0) return 0;

    /* cd <dirname>  →  enter subdirectory */
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (!(e[11] & 0x10)) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;
            cur_dir_cluster = *(u16 *)(e + 26);
            /* store human-readable name */
            name83_to_str(e, cur_dir_name);
            return 0;
        }
    }
    return -1;
}

int fat_mkdir(const char *name) {
    u16 cl = fat_alloc_cluster();
    if (!cl) return -2;

    /* Build "." and ".." entries in new cluster */
    u8 db[512];
    for (int i = 0; i < 512; i++) db[i] = 0;

    /* "." entry */
    for (int i = 0; i < 11; i++) db[i] = ' ';
    db[0] = '.';
    db[11] = 0x10;
    *(u16 *)(db + 26) = cl;

    /* ".." entry */
    u8 *dd = db + 32;
    for (int i = 0; i < 11; i++) dd[i] = ' ';
    dd[0] = '.'; dd[1] = '.';
    dd[11] = 0x10;
    *(u16 *)(dd + 26) = cur_dir_cluster;

    ata_write(data_sec + (cl - 2) * spc, 1, (u16 *)db);

    /* Create directory entry in current directory */
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] != 0 && e[0] != 0xE5) continue;
            for (int i = 0; i < 11; i++) e[i] = fname[i];
            e[11] = 0x10;
            *(u16 *)(e + 26) = cl;
            *(u32 *)(e + 28) = 0;
            write_curdir(s);
            return 0;
        }
    }
    return -1;
}

int fat_cat(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (e[11] & 0x10) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;
            u16 cl = *(u16 *)(e + 26);
            u32 size = *(u32 *)(e + 28);
            u8 dbuf[512];
            u32 remain = size;
            while (cl >= 2 && cl < 0xFF0 && remain) {
                ata_read(data_sec + (cl - 2) * spc, 1, (u16 *)dbuf);
                u32 chunk = spc * bps;
                if (chunk > remain) chunk = remain;
                for (u32 k = 0; k < chunk; k++) gfx_putc(dbuf[k]);
                remain -= chunk;
                cl = next_cluster(cl);
            }
            return 0;
        }
    }
    return -1;
}

void fat_cwd_str(char *out, int max) {
    if (max < 2) return;
    if (cur_dir_cluster == 0 || cur_dir_name[0] == 0) {
        out[0] = '\\'; out[1] = 0;
        return;
    }
    int n = 0;
    out[n++] = '\\';
    for (int i = 0; cur_dir_name[i] && n < max - 1; i++)
        out[n++] = cur_dir_name[i];
    out[n] = 0;
}

int fat_rmdir(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int ss = 0; ss < maxs; ss++) {
        if (read_curdir(ss)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (!(e[11] & 0x10)) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            /* Found the directory. Check it's empty. */
            u16 dir_cl = *(u16 *)(e + 26);
            u8 check[512];
            ata_read(data_sec + (dir_cl - 2) * spc, 1, (u16 *)check);
            for (int k = 64; k < 512; k += 32) {
                if (check[k] != 0 && check[k] != 0xE5) return -2;  /* not empty */
                if (check[k] == 0) break;
            }

            /* Free cluster chain */
            u16 cl = dir_cl;
            while (cl >= 2 && cl < 0xFF0) {
                u16 next = next_cluster(cl);
                write_fat_entry(cl, 0);
                cl = next;
            }

            /* Mark entry as deleted */
            e[0] = 0xE5;
            write_curdir(ss);
            return 0;
        }
    }
    return -1;
}

int fat_rename(const char *old_name, const char *new_name) {
    u8 old83[11], new83[11];
    str_to_name83(old_name, old83);
    str_to_name83(new_name, new83);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != old83[k]) { match = 0; break; }
            if (!match) continue;

            /* Rename entry in-place */
            for (int k = 0; k < 11; k++) e[k] = new83[k];
            write_curdir(s);
            return 0;
        }
    }
    return -1;
}
