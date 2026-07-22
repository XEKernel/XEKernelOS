/* 准则五: User-space FAT12 implementation (read-only)
   Uses SYS_DISK_READ raw sector reads. Self-contained — no kernel dependency. */

#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"

#include "ufs.h"
#include "libc.h"
#include "lib/types.h"

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
static u16  cur_dir_cluster = 0;

/* Read one 512-byte sector into buf. Returns 0 on success, -1 on error.
   Uses volatile + memory clobber to defeat compiler return-value optimization. */
static int disk_read(u32 lba, u8 *buf) {
    volatile int result = -1;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(SYS_DISK_READ), "b"((int)lba), "c"((int)buf)
        : "memory"
    );
    return result;
}

/* Convert FAT12 directory entry (11-char 8.3) to readable string */
static void name83_to_str(u8 *e, char *name) {
    int j = 0;
    for (int k = 0; k < 8 && e[k] != ' '; k++) name[j++] = e[k];
    if (e[8] != ' ') { name[j++] = '.'; for (int k = 8; k < 11 && e[k] != ' '; k++) name[j++] = e[k]; }
    name[j] = 0;
    /* Strip trailing spaces from the name portion */
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

/* Compare two name strings case-insensitively */
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

/* Read FAT entry for given cluster */
static u16 read_fat(u16 cl) {
    u8 sec[512];
    u32 fat_offset = cl + (cl / 2);  /* 12-bit packed: 3 bytes per 2 entries */
    u32 sector = fat_sec + fat_offset / 512;
    u32 off = fat_offset % 512;
    disk_read(sector, sec);
    u16 val = sec[off] | (sec[off+1] << 8);
    if (cl & 1) val >>= 4;
    else val &= 0x0FFF;
    return (val >= 0xFF8) ? 0 : val;
}

/* Read one cluster's sector into buffer */
static int read_cluster_sector(u16 cl, int sec_idx, u8 *buf) {
    u32 lba = data_sec + (cl - 2) * bpb.spc + sec_idx;
    return disk_read(lba, buf);
}

int ufs_init() {
    u8 buf[512];
    /* Read BPB from sector 0 of second disk (hdb) */
    if (disk_read(0, buf) < 0) return -1;

    /* Parse BPB */
    for (int i = 0; i < (int)sizeof(fat_bpb); i++)
        ((u8 *)&bpb)[i] = buf[i];

    fat_sec  = bpb.reserved;             /* FAT starts after reserved sectors */
    root_sec = fat_sec + bpb.fats * bpb.fat16; /* root dir after FATs */
    data_sec = root_sec + (bpb.root_ents * 32) / bpb.bps; /* data after root */
    return 0;
}

int ufs_ls() {
    u8 sec[512];

    if (cur_dir_cluster == 0) {
        /* Root directory: fixed region */
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;  /* deleted */
                if (sec[i+11] & 0x08) continue; /* volume label */

                char name[13];
                name83_to_str(&sec[i], name);

                /* Output via serial */
                syscall4(SYS_WRITE, (int)name, 0, 0);
                if (sec[i+11] & 0x10) {
                    syscall4(SYS_WRITE, (int)" (dir)", 0, 0);
                } else {
                    u32 sz = sec[i+28] | (sec[i+29] << 8) | (sec[i+30] << 16) | (sec[i+31] << 24);
                    char szbuf[16];
                    int p = 0;
                    if (sz == 0) szbuf[p++] = '0';
                    else while (sz > 0 && p < 15) { szbuf[p++] = '0' + (sz % 10); sz /= 10; }
                    szbuf[p] = 0;
                    /* Reverse */
                    for (int k = 0; k < p/2; k++) { char t = szbuf[k]; szbuf[k] = szbuf[p-1-k]; szbuf[p-1-k] = t; }
                    syscall4(SYS_WRITE, (int)" ", 0, 0);
                    syscall4(SYS_WRITE, (int)szbuf, 0, 0);
                }
                syscall4(SYS_WRITE, (int)"\n", 0, 0);
            }
        }
    } else {
        /* Subdirectory: walk cluster chain */
        u16 cl = cur_dir_cluster;
        while (cl > 0 && cl < 0xFF8) {
            disk_read(data_sec + (cl - 2) * bpb.spc, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;
                char name[13];
                name83_to_str(&sec[i], name);
                syscall4(SYS_WRITE, (int)name, 0, 0);
                if (sec[i+11] & 0x10) {
                    syscall4(SYS_WRITE, (int)" (dir)", 0, 0);
                }
                syscall4(SYS_WRITE, (int)"\n", 0, 0);
            }
            cl = read_fat(cl);
        }
    }
    return 0;
}

/* Read entire file into buffer. Returns size or -1. */
int ufs_read_file(const char *name, char *buf, int max) {
    u8 sec[512];
    u8 fname[11];
    str_to_name83(name, fname);

    u16 found_cl = 0;
    u32 found_sz = 0;

    /* Search root dir or current subdir */
    if (cur_dir_cluster == 0) {
        int root_sectors = (bpb.root_ents * 32) / bpb.bps;
        for (int s = 0; s < root_sectors && !found_cl; s++) {
            disk_read(root_sec + s, sec);
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;
                if (sec[i+11] & 0x10) continue;  /* skip dirs */
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
            disk_read(data_sec + (cl - 2) * bpb.spc, sec);
            for (int i = 0; i < 512 && !found_cl; i += 32) {
                if (sec[i] == 0) break;
                if (sec[i] == 0xE5) continue;
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

    /* Read file data */
    if ((int)found_sz > max) found_sz = max;
    u32 off = 0;
    u16 cl = found_cl;
    while (cl > 0 && cl < 0xFF8 && off < found_sz) {
        int sectors = bpb.spc;
        for (int s = 0; s < sectors && off < found_sz; s++) {
            u8 data[512];
            disk_read(data_sec + (cl - 2) * bpb.spc + s, data);
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
    syscall4(SYS_WRITE, (int)fbuf, 0, 0);
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

/* Current working directory path tracking */
static char cwd_path[64] __attribute__((section(".data"))) = "\\";

void ufs_cwd(char *out, int max) {
    int i = 0;
    while (cwd_path[i] && i < max - 1) { out[i] = cwd_path[i]; i++; }
    out[i] = 0;
}

int ufs_cd(const char *name) {
    if (!name || name[0] == 0) return 0;
    if ((name[0] == '\\' || name[0] == '/') && name[1] == 0) {
        cur_dir_cluster = 0;
        cwd_path[0] = '\\'; cwd_path[1] = 0;
        return 0;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        cur_dir_cluster = 0;
        cwd_path[0] = '\\'; cwd_path[1] = 0;
        return 0;
    }
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
                if (!(sec[i+11] & 0x10)) continue;
                int match = 1;
                for (int k = 0; k < 11; k++)
                    if (sec[i+k] != fname[k]) { match = 0; break; }
                if (match) {
                    cur_dir_cluster = sec[i+26] | (sec[i+27] << 8);
                    int j = 0; while (cwd_path[j]) j++;
                    if (j > 0 && cwd_path[j-1] != '\\') cwd_path[j++] = '\\';
                    int n = 0; while (name[n] && j < 63) cwd_path[j++] = name[n++];
                    cwd_path[j] = 0;
                    return 0;
                }
            }
        }
    }
    return -1;
}
