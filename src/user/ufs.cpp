/* 准则五: User-space FAT12 — read-only + directory navigation
   Uses SYS_DISK_READ raw sector reads. Screen output via SYS_GFX_*.
   Self-contained — no kernel dependency beyond raw I/O. */

#pragma clang diagnostic ignored "-Wunused-function"

#include "lib/types.h"

/* Syscall numbers and inline wrappers — embedded to avoid include chain issues */
#define U_SYS_DISK_READ   44
#define U_SYS_DISK_WRITE  45
#define U_SYS_FWRITE      36
#define U_SYS_IOCTL       46
#define U_FB_FD            0
#define U_IOCTL_SET_FG     1
#define U_IOCTL_CLS        2

#define COLOR_BLACK   0x00
#define COLOR_DGRAY   0x08
#define COLOR_LGRAY   0x07
#define COLOR_LRED    0x0C
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_YELLOW  0x0E
#define COLOR_WHITE   0x0F

static void _raw_syscall(int num, int a1, int a2, int a3) {
    __asm__ volatile("int $0x80" : : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
}

static void disk_read(u32 lba, u8 *buf) {
    _raw_syscall(U_SYS_DISK_READ, (int)lba, (int)buf, 0);
}

static void gfx_set_fg(int c)  { _raw_syscall(U_SYS_IOCTL, U_FB_FD, U_IOCTL_SET_FG, c); }
static void gfx_putc(char c)   {
    char tmp[2] = {c, 0};
    __asm__ volatile("int $0x80" : : "a"(U_SYS_FWRITE), "b"(U_FB_FD), "c"((int)tmp), "d"(1) : "memory");
}
static void gfx_puts(const char *s) {
    int l=0; while(s[l]) l++;
    __asm__ volatile("int $0x80" : : "a"(U_SYS_FWRITE), "b"(U_FB_FD), "c"((int)s), "d"(l) : "memory");
}

/* FAT12 BPB layout */
struct fat_bpb {
    u8  jmp[3];
    char oem[8];
    u16 bps;
    u8  spc;
    u16 reserved;
    u8  fats;
    u16 root_ents;
    u16 total16;
    u8  media;
    u16 fat16;
    u16 sectors;
    u8  drivenum;
    u8  reserved2;
    u8  bootsig;
    u32 volid;
    char label[11];
    char fstype[8];
} __attribute__((packed));

static fat_bpb bpb;
static int  data_sec, fat_sec, root_sec;
static u16  cur_dir_cluster  = 0;
static u16  parent_cluster   = 0;
static char cwd_path[64]     = "\\";

/* Convert FAT12 8.3 name to readable string */
static void name83_to_str(const u8 *e, char *name) {
    int j = 0;
    for (int k = 0; k < 8 && e[k] != ' '; k++) name[j++] = e[k];
    if (e[8] != ' ') { name[j++] = '.'; for (int k = 8; k < 11 && e[k] != ' '; k++) name[j++] = e[k]; }
    name[j] = 0;
    while (j > 0 && name[j-1] == ' ') { j--; name[j] = 0; }
}

/* Convert "NAME.EXT" to FAT12 8.3 format */
static void str_to_name83(const char *name, u8 *out) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (name[j] && name[j] != '.' && i < 8) out[i++] = name[j++];
    if (name[j] == '.') { j++; i = 8; }
    while (name[j] && i < 11) out[i++] = name[j++];
}

/* Case-insensitive name comparison */
static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Read FAT entry */
static u16 read_fat(u16 cl) {
    u8 sec[512];
    u32 fat_offset = cl + (cl / 2);
    u32 sector = fat_sec + fat_offset / 512;
    u32 off = fat_offset % 512;
    disk_read(sector, sec);
    u16 val = sec[off] | (sec[off+1] << 8);
    if (cl & 1) val >>= 4;
    else val &= 0x0FFF;
    return (val >= 0xFF8) ? 0 : val;
}

/* Read one cluster-sector into buf */
static void read_cluster_sector(u16 cl, int sec_idx, u8 *buf) {
    u32 lba = data_sec + (cl - 2) * bpb.spc + sec_idx;
    disk_read(lba, buf);
}

/* ==================== Init ==================== */

int ufs_init() {
    u8 buf[512];
    disk_read(0, buf);

    /* Validate BPB: check OEM signature */
    if (buf[3] != 'X' || buf[4] != 'E' || buf[5] != 'K')
        return -1;

    for (int i = 0; i < (int)sizeof(fat_bpb); i++)
        ((u8 *)&bpb)[i] = buf[i];

    fat_sec  = bpb.reserved;
    root_sec = fat_sec + bpb.fats * bpb.fat16;
    data_sec = root_sec + (bpb.root_ents * 32) / bpb.bps;
    return 0;
}

/* ==================== LS ==================== */

static void ls_print_entry(const u8 *e) {
    /* Skip volume label */
    if (e[11] & 0x08) return;

    char name[13];
    name83_to_str(e, name);

    u32 sz = e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24);

    /* Type indicator */
    if (e[11] & 0x10) {
        gfx_set_fg(COLOR_LCYAN);
        gfx_puts("DIR  ");
    } else {
        gfx_set_fg(COLOR_LGRAY);
        gfx_puts("     ");
    }

    /* Size (right-aligned, 9 chars) */
    char sbuf[10];
    int p = 8;
    for (int x = 0; x < 9; x++) sbuf[x] = ' ';
    u32 t = sz;
    if (!t) { sbuf[8] = '0'; }
    else while (t && p >= 0) { sbuf[p--] = '0' + (t % 10); t /= 10; }
    sbuf[9] = 0;
    gfx_set_fg(COLOR_DGRAY);
    gfx_puts(sbuf);

    /* Date & time (FAT12: time at offset 22, date at offset 24) */
    u16 fat_time = e[22] | (e[23] << 8);
    u16 fat_date = e[24] | (e[25] << 8);
    int hour = (fat_time >> 11) & 0x1F;
    int min  = (fat_time >> 5)  & 0x3F;
    int year = 1980 + ((fat_date >> 9) & 0x7F);
    int mon  = (fat_date >> 5) & 0x0F;
    int day  = fat_date & 0x1F;

    if (fat_date || fat_time) {
        gfx_set_fg(COLOR_YELLOW);
        gfx_putc(' ');
        char ts[18];
        int ti = 0;
        ts[ti++] = '0' + (year / 1000);
        ts[ti++] = '0' + ((year / 100) % 10);
        ts[ti++] = '0' + ((year / 10) % 10);
        ts[ti++] = '0' + (year % 10);
        ts[ti++] = '-';
        ts[ti++] = '0' + (mon / 10);
        ts[ti++] = '0' + (mon % 10);
        ts[ti++] = '-';
        ts[ti++] = '0' + (day / 10);
        ts[ti++] = '0' + (day % 10);
        ts[ti++] = ' ';
        ts[ti++] = '0' + (hour / 10);
        ts[ti++] = '0' + (hour % 10);
        ts[ti++] = ':';
        ts[ti++] = '0' + (min / 10);
        ts[ti++] = '0' + (min % 10);
        ts[ti] = 0;
        gfx_puts(ts);
    } else {
        /* No timestamp → blank */
        gfx_set_fg(COLOR_DGRAY);
        gfx_puts("                   ");
    }

    /* Name */
    gfx_set_fg(COLOR_WHITE);
    gfx_putc(' ');
    gfx_puts(name);
    gfx_putc('\n');
    gfx_set_fg(COLOR_LGRAY);
}

int ufs_ls() {
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        /* Root directory: fixed region */
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return 0;
                if (sec[i] == 0xE5) continue;
                ls_print_entry(&sec[i]);
            }
        }
    } else {
        /* Subdirectory: walk cluster chain */
        u16 cl = cur_dir_cluster;
        while (cl > 0 && cl < 0xFF8) {
            /* First sector of cluster */
            read_cluster_sector(cl, 0, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return 0;
                if (sec[i] == 0xE5) continue;
                ls_print_entry(&sec[i]);
            }
            cl = read_fat(cl);
        }
    }
    return 0;
}

/* ==================== CD ==================== */

int ufs_cd(const char *name) {
    if (!name || name[0] == 0) return 0;

    /* Root */
    if ((name[0] == '\\' || name[0] == '/') && name[1] == 0) {
        cur_dir_cluster = 0;
        parent_cluster  = 0;
        cwd_path[0] = '\\'; cwd_path[1] = 0;
        return 0;
    }

    /* Parent directory: use stored parent cluster */
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        if (cur_dir_cluster == 0) return 0;  /* already at root */

        cur_dir_cluster = parent_cluster;

        /* Strip last path component */
        int len = 0; while (cwd_path[len]) len++;
        if (len > 1) {
            while (len > 1 && cwd_path[len-1] != '\\') len--;
            if (len > 1) len--;
            cwd_path[len] = 0;
        } else {
            cwd_path[0] = '\\'; cwd_path[1] = 0;
        }

        /* Read parent cluster's .. entry to get grandparent */
        if (cur_dir_cluster != 0) {
            u8 sec[512];
            read_cluster_sector(cur_dir_cluster, 0, sec);
            /* .. entry is at offset 32 */
            parent_cluster = sec[32+26] | (sec[32+27] << 8);
        } else {
            parent_cluster = 0;
        }
        return 0;
    }

    /* Subdirectory */
    u8 fname[11];
    str_to_name83(name, fname);
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        /* Search root dir */
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                if (!(sec[i+11] & 0x10)) continue;  /* not a dir */
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    u16 new_cl = sec[i+26] | (sec[i+27] << 8);
                    parent_cluster = cur_dir_cluster;  /* root = 0 */
                    cur_dir_cluster = new_cl;

                    /* Append to cwd_path */
                    int j = 0; while (cwd_path[j]) j++;
                    if (j > 0 && cwd_path[j-1] != '\\') cwd_path[j++] = '\\';
                    int n = 0; while (name[n] && j < 63) cwd_path[j++] = name[n++];
                    cwd_path[j] = 0;
                    return 0;
                }
            }
        }
    } else {
        /* Search subdirectory cluster chain */
        u16 cl = cur_dir_cluster;
        while (cl > 0 && cl < 0xFF8) {
            read_cluster_sector(cl, 0, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                if (!(sec[i+11] & 0x10)) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    u16 new_cl = sec[i+26] | (sec[i+27] << 8);
                    parent_cluster = cur_dir_cluster;
                    cur_dir_cluster = new_cl;

                    /* Append to cwd_path */
                    int j = 0; while (cwd_path[j]) j++;
                    if (j > 0 && cwd_path[j-1] != '\\') cwd_path[j++] = '\\';
                    int n = 0; while (name[n] && j < 63) cwd_path[j++] = name[n++];
                    cwd_path[j] = 0;
                    return 0;
                }
            }
            cl = read_fat(cl);
        }
    }
    return -1;
}

/* ==================== CWD ==================== */

void ufs_cwd(char *out, int max) {
    int i = 0;
    while (cwd_path[i] && i < max - 1) { out[i] = cwd_path[i]; i++; }
    out[i] = 0;
}

/* ==================== CAT / READ ==================== */

int ufs_read_file(const char *name, char *buf, int max) {
    u8 sec[512];
    u8 fname[11];
    str_to_name83(name, fname);

    u16 found_cl = 0;
    u32 found_sz = 0;

    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors && !found_cl; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;
                if (sec[i+11] & 0x10) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    found_cl = sec[i+26] | (sec[i+27] << 8);
                    found_sz = sec[i+28] | (sec[i+29] << 8) | (sec[i+30] << 16) | (sec[i+31] << 24);
                    break;
                }
            }
        }
    } else {
        u16 cl = cur_dir_cluster;
        while (cl > 0 && cl < 0xFF8 && !found_cl) {
            read_cluster_sector(cl, 0, sec);
            for (int i = 0; i < 512 && !found_cl; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;
                if (sec[i+11] & 0x10) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    found_cl = sec[i+26] | (sec[i+27] << 8);
                    found_sz = sec[i+28] | (sec[i+29] << 8) | (sec[i+30] << 16) | (sec[i+31] << 24);
                    break;
                }
            }
            cl = read_fat(cl);
        }
    }

    if (!found_cl) return -1;
    if ((int)found_sz > max) found_sz = (u32)max;

    u32 off = 0;
    u16 cl = found_cl;
    while (cl > 0 && cl < 0xFF8 && off < found_sz) {
        int sectors = bpb.spc;
        for (int s = 0; s < sectors && off < found_sz; s++) {
            u8 data[512];
            read_cluster_sector(cl, s, data);
            u32 chunk = 512;
            if (off + chunk > found_sz) chunk = found_sz - off;
            for (u32 k = 0; k < chunk; k++) buf[off++] = data[k];
        }
        cl = read_fat(cl);
    }
    buf[off] = 0;
    return (int)off;
}

int ufs_cat(const char *name) {
    static char fbuf[2048] __attribute__((section(".data"))) = {0};
    int sz = ufs_read_file(name, fbuf, 2047);
    if (sz <= 0) return -1;
    fbuf[sz] = 0;
    gfx_puts(fbuf);
    return 0;
}

int ufs_size(const char *name) {
    u8 sec[512];
    u8 fname[11];
    str_to_name83(name, fname);

    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match)
                    return sec[i+28] | (sec[i+29] << 8) | (sec[i+30] << 16) | (sec[i+31] << 24);
            }
        }
    }
    return -1;
}

/* ==================== Write Support (准则五) ==================== */

static void disk_write(u32 lba, const u8 *buf) {
    _raw_syscall(U_SYS_DISK_WRITE, (int)lba, (int)buf, 0);
}

static void write_fat_entry(u16 cl, u16 value) {
    u32 off = cl + (cl / 2);
    u32 fat_sec_off = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    disk_read(fat_sec + fat_sec_off, fbuf);
    u16 val = fbuf[fat_pos] | (fbuf[fat_pos+1] << 8);
    if (cl & 1)
        val = (val & 0x000F) | ((value & 0xFFF) << 4);
    else
        val = (val & 0xF000) | (value & 0xFFF);
    fbuf[fat_pos]   = val & 0xFF;
    fbuf[fat_pos+1] = (val >> 8) & 0xFF;
    disk_write(fat_sec + fat_sec_off, fbuf);
    /* Write second FAT copy for redundancy */
    for (int i = 1; i < bpb.fats; i++)
        disk_write(fat_sec + i * bpb.fat16 + fat_sec_off, fbuf);
}

static u16 alloc_cluster() {
    u16 max_cl = (bpb.fat16 * 512 * 2) / 3;
    for (u16 cl = 2; cl < max_cl; cl++)
        if (read_fat(cl) == 0) {
            write_fat_entry(cl, 0xFFF);
            return cl;
        }
    return 0;
}

static void free_cluster_chain(u16 cl) {
    while (cl >= 2 && cl < 0xFF0) {
        u16 next = read_fat(cl);
        write_fat_entry(cl, 0);
        cl = next;
    }
}

/* Read current directory sector into buf. Returns 0 on success. */
static int read_curdir_sector(int sec_idx, u8 *buf) {
    if (cur_dir_cluster == 0) {
        disk_read(root_sec + sec_idx, buf);
        return 0;
    }
    u16 cl = cur_dir_cluster;
    int per = bpb.spc;
    int idx = sec_idx;
    while (idx >= per) {
        u16 nx = read_fat(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx;
        idx -= per;
    }
    read_cluster_sector(cl, idx, buf);
    return 0;
}

/* Write buf to current directory sector. Returns 0 on success. */
static int write_curdir_sector(int sec_idx, const u8 *buf) {
    if (cur_dir_cluster == 0) {
        disk_write(root_sec + sec_idx, buf);
        return 0;
    }
    u16 cl = cur_dir_cluster;
    int per = bpb.spc;
    int idx = sec_idx;
    while (idx >= per) {
        u16 nx = read_fat(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx;
        idx -= per;
    }
    u32 lba = data_sec + (cl - 2) * bpb.spc + idx;
    disk_write(lba, buf);
    return 0;
}

/* Find a free directory entry slot. Returns byte offset or -1.
   If the directory is full, returns -1. */
static int dir_find_free_slot() {
    int root_sectors = (bpb.root_ents * 32) / bpb.bps;
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0 || sec[i] == 0xE5)
                    return s * 512 + i;
            }
        }
    } else {
        int sec_idx = 0;
        u16 cl = cur_dir_cluster;
        while (cl >= 2 && cl < 0xFF0) {
            for (int si = 0; si < bpb.spc; si++) {
                read_cluster_sector(cl, si, sec);
                for (int i = 0; i < 512; i += 32) {
                    if (sec[i] == 0 || sec[i] == 0xE5)
                        return sec_idx * 512 + i;
                }
                sec_idx++;
            }
            cl = read_fat(cl);
        }
    }
    return -1;
}

/* ---- CREATE (write_file) ---- */

int ufs_create(const char *name, const char *data, int size) {
    if (size <= 0 || size > 4096) return -1;

    u8 fname[11];
    str_to_name83(name, fname);

    int slot = dir_find_free_slot();
    if (slot < 0) return -2;

    /* Allocate clusters for data */
    int cpb = bpb.spc * bpb.bps;  /* bytes per cluster */
    int needed = (size + cpb - 1) / cpb;
    u16 first_cl = 0, prev_cl = 0;
    for (int i = 0; i < needed; i++) {
        u16 cl = alloc_cluster();
        if (!cl) { if (first_cl) free_cluster_chain(first_cl); return -3; }
        if (!first_cl) first_cl = cl;
        if (prev_cl)  write_fat_entry(prev_cl, cl);
        prev_cl = cl;
    }

    /* Write data to allocated clusters */
    int written = 0;
    u16 cl = first_cl;
    while (cl >= 2 && cl < 0xFF0 && written < size) {
        u8 buf[512];
        int chunk = 512;
        if (chunk > size - written) chunk = size - written;
        for (int k = 0; k < chunk; k++) buf[k] = (u8)data[written + k];
        for (int k = chunk; k < 512; k++) buf[k] = 0;
        u32 lba = data_sec + (cl - 2) * bpb.spc;
        for (int si = 0; si < bpb.spc && written < size; si++) {
            if (si > 0) {
                chunk = 512;
                if (chunk > size - written) chunk = size - written;
                for (int k = 0; k < chunk; k++) buf[k] = (u8)data[written + k];
                for (int k = chunk; k < 512; k++) buf[k] = 0;
            }
            disk_write(lba + si, buf);
            written += chunk;
        }
        cl = read_fat(cl);
    }

    /* Write directory entry */
    int sec_idx = slot / 512;
    int off = slot % 512;
    u8 sec[512];
    read_curdir_sector(sec_idx, sec);
    u8 *entry = sec + off;
    for (int k = 0; k < 11; k++) entry[k] = fname[k];
    entry[11] = 0x20;
    entry[12] = 0;  /* reserved */
    entry[26] = first_cl & 0xFF;
    entry[27] = (first_cl >> 8) & 0xFF;
    entry[28] = size & 0xFF;
    entry[29] = (size >> 8) & 0xFF;
    entry[30] = (size >> 16) & 0xFF;
    entry[31] = (size >> 24) & 0xFF;
    write_curdir_sector(sec_idx, sec);
    return 0;
}

/* ---- DELETE (rm) ---- */

int ufs_rm(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                if (sec[i+11] & 0x10) continue;  /* skip dirs */
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    u16 cl = sec[i+26] | (sec[i+27] << 8);
                    free_cluster_chain(cl);
                    sec[i] = 0xE5;
                    disk_write(root_sec + s, sec);
                    return 0;
                }
            }
        }
    } else {
        /* Subdirectory: walk cluster chain */
        int sec_idx = 0;
        u16 cl = cur_dir_cluster;
        while (cl >= 2 && cl < 0xFF0) {
            for (int si = 0; si < bpb.spc; si++) {
                read_cluster_sector(cl, si, sec);
                for (int i = 0; i < 512; i += 32) {
                    if (sec[i] == 0) return -1;
                    if (sec[i] == 0xE5) continue;
                    if (sec[i+11] & 0x10) continue;
                    int match = 1;
                    for (int k = 0; k < 11; k++)
                        if (sec[i+k] != fname[k]) { match = 0; break; }
                    if (match) {
                        u16 fc = sec[i+26] | (sec[i+27] << 8);
                        free_cluster_chain(fc);
                        sec[i] = 0xE5;
                        write_curdir_sector(sec_idx, sec);
                        return 0;
                    }
                }
                sec_idx++;
            }
            cl = read_fat(cl);
        }
    }
    return -1;
}

/* ---- MKDIR ---- */

int ufs_mkdir(const char *name) {
    u16 cl = alloc_cluster();
    if (!cl) return -2;

    /* Create . and .. entries in new directory */
    u8 db[512];
    for (int i = 0; i < 512; i++) db[i] = 0;

    /* . entry */
    for (int i = 0; i < 11; i++) db[i] = ' ';
    db[0] = '.';
    db[11] = 0x10;  /* directory attribute */
    db[26] = cl & 0xFF;
    db[27] = (cl >> 8) & 0xFF;

    /* .. entry */
    u8 *dd = db + 32;
    for (int i = 0; i < 11; i++) dd[i] = ' ';
    dd[0] = '.'; dd[1] = '.';
    dd[11] = 0x10;
    dd[26] = cur_dir_cluster & 0xFF;
    dd[27] = (cur_dir_cluster >> 8) & 0xFF;

    u32 lba = data_sec + (cl - 2) * bpb.spc;
    disk_write(lba, db);

    /* Write directory entry in current directory */
    u8 fname[11];
    str_to_name83(name, fname);

    int slot = dir_find_free_slot();
    if (slot < 0) { write_fat_entry(cl, 0); return -3; }

    int sec_idx = slot / 512;
    int off = slot % 512;
    u8 sec[512];
    read_curdir_sector(sec_idx, sec);
    u8 *entry = sec + off;
    for (int k = 0; k < 11; k++) entry[k] = fname[k];
    entry[11] = 0x10;
    entry[26] = cl & 0xFF;
    entry[27] = (cl >> 8) & 0xFF;
    for (int k = 28; k < 32; k++) entry[k] = 0;
    write_curdir_sector(sec_idx, sec);
    return 0;
}

/* ---- RMDIR (remove empty directory) ---- */

int ufs_rmdir(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                if (!(sec[i+11] & 0x10)) continue;  /* not a dir */
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    u16 cl = sec[i+26] | (sec[i+27] << 8);

                    /* Verify directory is empty (only . and .. entries) */
                    u8 db[512];
                    u32 lba = data_sec + (cl - 2) * bpb.spc;
                    disk_read(lba, db);
                    for (int i = 64; i < 512; i += 32) {
                        if (db[i] != 0 && db[i] != 0xE5) return -2;  /* not empty */
                    }

                    free_cluster_chain(cl);
                    sec[i] = 0xE5;
                    disk_write(root_sec + s, sec);
                    return 0;
                }
            }
        }
    } else {
        int sec_idx = 0;
        u16 cl = cur_dir_cluster;
        while (cl >= 2 && cl < 0xFF0) {
            for (int si = 0; si < bpb.spc; si++) {
                read_cluster_sector(cl, si, sec);
                for (int i = 0; i < 512; i += 32) {
                    if (sec[i] == 0) return -1;
                    if (sec[i] == 0xE5) continue;
                    if (!(sec[i+11] & 0x10)) continue;
                    int match = 1;
                    for (int k = 0; k < 11; k++)
                        if (sec[i+k] != fname[k]) { match = 0; break; }
                    if (match) {
                        u16 dc = sec[i+26] | (sec[i+27] << 8);

                        /* Check empty */
                        u8 db[512];
                        u32 lba2 = data_sec + (dc - 2) * bpb.spc;
                        disk_read(lba2, db);
                        for (int j = 64; j < 512; j += 32) {
                            if (db[j] != 0 && db[j] != 0xE5) return -2;
                        }

                        free_cluster_chain(dc);
                        sec[i] = 0xE5;
                        write_curdir_sector(sec_idx, sec);
                        return 0;
                    }
                }
                sec_idx++;
            }
            cl = read_fat(cl);
        }
    }
    return -1;
}

/* ---- MV (rename) ---- */

int ufs_mv(const char *old_name, const char *new_name) {
    u8 old_fname[11], new_fname[11];
    str_to_name83(old_name, old_fname);
    str_to_name83(new_name, new_fname);
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) return -1;
                if (sec[i] == 0xE5) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != old_fname[k]) { match = 0; break; }
                if (match) {
                    for (int k = 0; k < 11; k++) sec[i+k] = new_fname[k];
                    disk_write(root_sec + s, sec);
                    return 0;
                }
            }
        }
    } else {
        int sec_idx = 0;
        u16 cl = cur_dir_cluster;
        while (cl >= 2 && cl < 0xFF0) {
            for (int si = 0; si < bpb.spc; si++) {
                read_cluster_sector(cl, si, sec);
                for (int i = 0; i < 512; i += 32) {
                    if (sec[i] == 0) return -1;
                    if (sec[i] == 0xE5) continue;
                    int match = 1;
                    for (int k = 0; k < 11; k++)
                        if (sec[i+k] != old_fname[k]) { match = 0; break; }
                    if (match) {
                        for (int k = 0; k < 11; k++) sec[i+k] = new_fname[k];
                        write_curdir_sector(sec_idx, sec);
                        return 0;
                    }
                }
                sec_idx++;
            }
            cl = read_fat(cl);
        }
    }
    return -1;
}
