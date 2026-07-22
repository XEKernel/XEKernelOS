#include "fs/fat12.h"
#include "drivers/ata.h"
#include "drivers/bcache.h"
#include "drivers/gfx.h"
#include "lib/types.h"

FatFilesystem fat;

/* ---- internal helpers ---- */

int FatFilesystem::read_root_sec(int sector, u8 *dst) {
    return bc_read(root_secs_ + sector, 1, (u16 *)dst);
}

int FatFilesystem::write_root_sec(int sector, u8 *src) {
    return bc_write(root_secs_ + sector, 1, (u16 *)src);
}

u16 FatFilesystem::next_cluster(u16 cl) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    bc_read(start_sec_ + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1) return val >> 4;
    return val & 0xFFF;
}

u16 FatFilesystem::read_fat_entry(u16 cl) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    bc_read(start_sec_ + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1) return val >> 4;
    return val & 0xFFF;
}

void FatFilesystem::write_fat_entry(u16 cl, u16 value) {
    u32 off = cl + (cl / 2);
    u32 fat_sec = off / 512;
    u32 fat_pos = off % 512;
    u8 fbuf[512];
    bc_read(start_sec_ + fat_sec, 1, (u16 *)fbuf);
    u16 val = *(u16 *)(fbuf + fat_pos);
    if (cl & 1)
        val = (val & 0x000F) | ((value & 0xFFF) << 4);
    else
        val = (val & 0xF000) | (value & 0xFFF);
    *(u16 *)(fbuf + fat_pos) = val;
    bc_write(start_sec_ + fat_sec, 1, (u16 *)fbuf);
    for (int i = 1; i < num_fat_; i++)
        bc_write(start_sec_ + i * fat_secs_ + fat_sec, 1, (u16 *)fbuf);
}

void FatFilesystem::name83_to_str(u8 *e, char *name) {
    int n = 0;
    for (int k = 0; k < 8 && e[k] != ' '; k++) name[n++] = e[k];
    name[n++] = '.';
    for (int k = 8; k < 11 && e[k] != ' '; k++) name[n++] = e[k];
    if (name[n-1] == '.') n--;
    name[n] = 0;
}

void FatFilesystem::str_to_name83(const char *name, u8 *fname) {
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

int FatFilesystem::curdir_secs() {
    if (cur_dir_cluster_ == 0)
        return data_sec_ - root_secs_;
    int n = 0;
    u16 cl = cur_dir_cluster_;
    while (cl >= 2 && cl < 0xFF0) { n += spc_; cl = next_cluster(cl); }
    return n;
}

int FatFilesystem::read_curdir(int sec_idx) {
    if (cur_dir_cluster_ == 0)
        return read_root_sec(sec_idx, buf_);
    u16 cl = cur_dir_cluster_;
    int per = spc_;
    while (sec_idx >= per) {
        u16 nx = next_cluster(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx; sec_idx -= per;
    }
    return bc_read(data_sec_ + (cl - 2) * spc_ + sec_idx, 1, (u16 *)buf_);
}

int FatFilesystem::write_curdir(int sec_idx) {
    if (cur_dir_cluster_ == 0)
        return write_root_sec(sec_idx, buf_);
    u16 cl = cur_dir_cluster_;
    int per = spc_;
    while (sec_idx >= per) {
        u16 nx = next_cluster(cl);
        if (nx < 2 || nx >= 0xFF0) return -1;
        cl = nx; sec_idx -= per;
    }
    return bc_write(data_sec_ + (cl - 2) * spc_ + sec_idx, 1, (u16 *)buf_);
}

/* ---- public API ---- */

int FatFilesystem::init() {
    int r = bc_read(0, 1, (u16 *)buf_);
    if (r) return -1;
    bps_ = *(u16 *)(buf_ + 11);
    spc_ = buf_[13];
    u16 reserved = *(u16 *)(buf_ + 14);
    num_fat_ = buf_[16];
    root_ents_ = *(u16 *)(buf_ + 17);
    u16 fat_size  = *(u16 *)(buf_ + 22);
    start_sec_ = reserved;
    fat_secs_  = fat_size;
    root_secs_ = reserved + num_fat_ * fat_size;
    data_sec_  = root_secs_ + (root_ents_ * 32 + bps_ - 1) / bps_;
    return 0;
}

u16 FatFilesystem::find_free_cluster() {
    u16 max_cluster = (fat_secs_ * 512 * 2) / 3;
    for (u16 cl = 2; cl < max_cluster; cl++)
        if (read_fat_entry(cl) == 0) return cl;
    return 0;
}

u16 FatFilesystem::alloc_cluster() {
    u16 cl = find_free_cluster();
    if (!cl) return 0;
    write_fat_entry(cl, 0xFFF);
    return cl;
}

void FatFilesystem::set_cluster(u16 cluster, u16 value) {
    write_fat_entry(cluster, value);
}

int FatFilesystem::read_file(const char *name, u8 *out, u32 max_len) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (e[11] & 0x10) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            u16 cl = *(u16 *)(e + 26);
            u32 size = *(u32 *)(e + 28);
            u32 remain = (size < max_len) ? size : max_len;
            u32 offset = 0;
            while (cl >= 2 && cl < 0xFF0 && remain) {
                u32 chunk = spc_ * bps_;
                if (chunk > remain) chunk = remain;
                bc_read(data_sec_ + (cl - 2) * spc_, (chunk + bps_ - 1) / bps_, (u16 *)(out + offset));
                offset += chunk;
                remain -= chunk;
                cl = next_cluster(cl);
            }
            return (int)size;
        }
    }
    return -1;
}

int FatFilesystem::write_file(const char *name, const u8 *data, u32 size) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    int free_entry = -1;
    int found = 0;

    for (int s = 0; s < maxs && !found; s++) {
        if (read_curdir(s)) return -3;
        for (int j = 0; j < 512 && !found; j += 32) {
            u8 *e = buf_ + j;
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

    u32 needed_clusters = (size + spc_ * bps_ - 1) / (spc_ * bps_);
    u16 first_cl = 0, prev_cl = 0;
    for (u32 i = 0; i < needed_clusters; i++) {
        u16 cl = alloc_cluster();
        if (!cl) return -5;
        if (!first_cl) first_cl = cl;
        if (prev_cl) write_fat_entry(prev_cl, cl);
        prev_cl = cl;
    }

    u32 written = 0;
    u16 cl = first_cl;
    while (cl >= 2 && cl < 0xFF0 && written < size) {
        u32 chunk = spc_ * bps_;
        if (chunk > size - written) chunk = size - written;
        u32 sec_count = (chunk + bps_ - 1) / bps_;
        if (sec_count > 0)
            bc_write(data_sec_ + (cl - 2) * spc_, sec_count, (u16 *)(data + written));
        written += chunk;
        cl = next_cluster(cl);
    }

    int sec_idx = free_entry / 512;
    int entry_off = free_entry % 512;
    if (read_curdir(sec_idx)) return -3;
    u8 *entry = buf_ + entry_off;
    for (int k = 0; k < 11; k++) entry[k] = fname[k];
    entry[11] = 0x20;
    *(u16 *)(entry + 26) = first_cl;
    *(u32 *)(entry + 28) = size;
    write_curdir(sec_idx);
    return 0;
}

int FatFilesystem::delete_file(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) return -1;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
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

int FatFilesystem::dir() {
    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return 0;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            char name[13];
            name83_to_str(e, name);
            if (e[11] & 0x10) {
                gfx_set_fg(COLOR_LCYAN);
                gfx_puts("DIR  ");
            } else {
                gfx_set_fg(COLOR_LGRAY);
                gfx_puts("     ");
            }
            u32 sz = *(u32 *)(e + 28);
            char ss[10]; int p = 8;
            for (int x = 0; x < 9; x++) ss[x] = ' ';
            u32 t = sz; p = 8;
            if (!t) { ss[8] = '0'; }
            else while (t && p >= 0) { ss[p--] = '0' + (t % 10); t /= 10; }
            gfx_set_fg(COLOR_DGRAY);
            gfx_puts(ss);

            /* Timestamp */
            u16 fat_time = e[22] | (e[23] << 8);
            u16 fat_date = e[24] | (e[25] << 8);
            if (fat_date || fat_time) {
                int hour = (fat_time >> 11) & 0x1F;
                int min  = (fat_time >> 5)  & 0x3F;
                int year = 1980 + ((fat_date >> 9) & 0x7F);
                int mon  = (fat_date >> 5) & 0x0F;
                int day  = fat_date & 0x1F;
                char ts[18];
                int ti = 0;
                ts[ti++] = '0' + (year / 1000);
                ts[ti++] = '0' + ((year / 100) % 10);
                ts[ti++] = '0' + ((year / 10) % 10);
                ts[ti++] = '0' + (year % 10);
                ts[ti++] = '-';
                ts[ti++] = '0' + (mon / 10); ts[ti++] = '0' + (mon % 10);
                ts[ti++] = '-';
                ts[ti++] = '0' + (day / 10); ts[ti++] = '0' + (day % 10);
                ts[ti++] = ' ';
                ts[ti++] = '0' + (hour / 10); ts[ti++] = '0' + (hour % 10);
                ts[ti++] = ':';
                ts[ti++] = '0' + (min / 10); ts[ti++] = '0' + (min % 10);
                ts[ti] = 0;
                gfx_set_fg(COLOR_YELLOW);
                gfx_putc(' ');
                gfx_puts(ts);
            }

            gfx_set_fg(COLOR_WHITE);
            gfx_putc(' ');
            gfx_puts(name); gfx_putc('\n');
            gfx_set_fg(COLOR_LGRAY);
        }
    }
    return 0;
}

int FatFilesystem::cd(const char *name) {
    if (*name == '\\' && name[1] == 0) {
        cur_dir_cluster_ = 0;
        cur_dir_name_[0] = 0;
        return 0;
    }
    while (*name == ' ') name++;
    if (!*name) return -1;

    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        if (cur_dir_cluster_ == 0) return 0;
        if (read_curdir(0)) return -1;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) break;
            if (e[0] == 0xE5) continue;
            if (!(e[11] & 0x10)) continue;
            if (e[0] == '.' && e[1] == '.' && e[2] == ' ') {
                cur_dir_cluster_ = *(u16 *)(e + 26);
                /* Strip last path component from cur_dir_name_ */
                char *p = cur_dir_name_;
                char *last = p;
                while (*p) { if (*p == '\\') last = p; p++; }
                *last = 0;
                if (cur_dir_name_[0] == 0 && cur_dir_cluster_ != 0)
                    cur_dir_name_[0] = '?';
                return 0;
            }
        }
        return -1;
    }

    if (name[0] == '.' && name[1] == 0) return 0;

    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (!(e[11] & 0x10)) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;
            cur_dir_cluster_ = *(u16 *)(e + 26);
            /* Append subdirectory name to cur_dir_name_ */
            int n = 0;
            while (cur_dir_name_[n]) n++;
            if (n == 0) cur_dir_name_[n++] = '\\';
            else if (n > 0 && cur_dir_name_[n-1] != '\\') cur_dir_name_[n++] = '\\';
            name83_to_str(e, cur_dir_name_ + n);
            return 0;
        }
    }
    return -1;
}

int FatFilesystem::mkdir(const char *name) {
    u16 cl = alloc_cluster();
    if (!cl) return -2;

    u8 db[512];
    for (int i = 0; i < 512; i++) db[i] = 0;

    for (int i = 0; i < 11; i++) db[i] = ' ';
    db[0] = '.';
    db[11] = 0x10;
    *(u16 *)(db + 26) = cl;

    u8 *dd = db + 32;
    for (int i = 0; i < 11; i++) dd[i] = ' ';
    dd[0] = '.'; dd[1] = '.';
    dd[11] = 0x10;
    *(u16 *)(dd + 26) = cur_dir_cluster_;

    bc_write(data_sec_ + (cl - 2) * spc_, 1, (u16 *)db);

    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
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

int FatFilesystem::cat(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
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
            u8 dbuf[513];  // +1 for null terminator
            u32 remain = size;
            while (cl >= 2 && cl < 0xFF0 && remain) {
                bc_read(data_sec_ + (cl - 2) * spc_, 1, (u16 *)dbuf);
                u32 chunk = spc_ * bps_;
                if (chunk > remain) chunk = remain;
                dbuf[chunk] = 0;
                gfx_puts_utf8((const char *)dbuf);
                remain -= chunk;
                cl = next_cluster(cl);
            }
            return 0;
        }
    }
    return -1;
}

void FatFilesystem::cwd_str(char *out, int max) {
    if (max < 2) return;
    int n = 0;
    if (cur_dir_name_[0] == 0) {
        out[0] = '\\'; out[1] = 0;
    } else {
        while (cur_dir_name_[n] && n < max - 1) {
            out[n] = cur_dir_name_[n];
            n++;
        }
        out[n] = 0;
    }
}

int FatFilesystem::rmdir(const char *name) {
    u8 fname[11];
    str_to_name83(name, fname);

    int maxs = curdir_secs();
    for (int ss = 0; ss < maxs; ss++) {
        if (read_curdir(ss)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            if (!(e[11] & 0x10)) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != fname[k]) { match = 0; break; }
            if (!match) continue;

            u16 dir_cl = *(u16 *)(e + 26);
            u8 check[512];
            bc_read(data_sec_ + (dir_cl - 2) * spc_, 1, (u16 *)check);
            for (int k = 64; k < 512; k += 32) {
                if (check[k] != 0 && check[k] != 0xE5) return -2;
                if (check[k] == 0) break;
            }

            u16 cl = dir_cl;
            while (cl >= 2 && cl < 0xFF0) {
                u16 next = next_cluster(cl);
                write_fat_entry(cl, 0);
                cl = next;
            }

            e[0] = 0xE5;
            write_curdir(ss);
            return 0;
        }
    }
    return -1;
}

int FatFilesystem::rename(const char *old_name, const char *new_name) {
    u8 old83[11], new83[11];
    str_to_name83(old_name, old83);
    str_to_name83(new_name, new83);

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) break;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (e[k] != old83[k]) { match = 0; break; }
            if (!match) continue;

            for (int k = 0; k < 11; k++) e[k] = new83[k];
            write_curdir(s);
            return 0;
        }
    }
    return -1;
}

int FatFilesystem::stat(const char *name, int *is_dir) {
    u8 fname[11];
    str_to_name83(name, fname);
    *is_dir = 0;

    int maxs = curdir_secs();
    for (int s = 0; s < maxs; s++) {
        if (read_curdir(s)) return -1;
        for (int j = 0; j < 512; j += 32) {
            u8 *e = buf_ + j;
            if (e[0] == 0) return -1;
            if (e[0] == 0xE5) continue;
            if (e[11] & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) {
                if (e[k] != fname[k]) { match = 0; break; }
            }
            if (!match) continue;
            *is_dir = (e[11] & 0x10) ? 1 : 0;
            return *(u32 *)(e + 28);
        }
    }
    return -1;
}
