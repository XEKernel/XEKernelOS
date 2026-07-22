/* ramdisk — simple in-memory file system */

#include "fs/ramdisk.h"
#include "lib/heap.h"

RamDisk ramdisk;

void RamDisk::init() {
    for (int i = 0; i < RAMDISK_MAX_FILES; i++) {
        files_[i].used = false;
        files_[i].size = 0;
    }
}

int RamDisk::find_free() {
    for (int i = 0; i < RAMDISK_MAX_FILES; i++)
        if (!files_[i].used) return i;
    return -1;
}

int RamDisk::find_name(const char *name) {
    for (int i = 0; i < RAMDISK_MAX_FILES; i++) {
        if (!files_[i].used) continue;
        int j = 0;
        while (name[j] && j < RAMDISK_NAME_LEN - 1 && name[j] == files_[i].name[j]) j++;
        if (name[j] == 0 && files_[i].name[j] == 0) return i;
    }
    return -1;
}

int RamDisk::create(const char *name, const u8 *data, u32 size) {
    if (!name || size > RAMDISK_MAX_SIZE) return -1;

    /* Overwrite existing */
    int idx = find_name(name);
    if (idx < 0) {
        idx = find_free();
        if (idx < 0) return -1;
    }

    /* Copy name */
    int i = 0;
    while (name[i] && i < RAMDISK_NAME_LEN - 1) {
        files_[idx].name[i] = name[i];
        i++;
    }
    files_[idx].name[i] = 0;

    /* Copy data */
    if (data && size > 0) {
        for (u32 j = 0; j < size; j++)
            files_[idx].data[j] = data[j];
    }
    files_[idx].size = size;
    files_[idx].used = true;
    return 0;
}

int RamDisk::read(const char *name, u8 *out, u32 max) {
    int idx = find_name(name);
    if (idx < 0) return -1;
    u32 sz = files_[idx].size;
    if (sz > max) sz = max;
    for (u32 i = 0; i < sz; i++)
        out[i] = files_[idx].data[i];
    return (int)sz;
}

int RamDisk::remove(const char *name) {
    int idx = find_name(name);
    if (idx < 0) return -1;
    files_[idx].used = false;
    files_[idx].size = 0;
    return 0;
}

int RamDisk::list(char *out, u32 max) {
    int pos = 0;
    for (int i = 0; i < RAMDISK_MAX_FILES; i++) {
        if (!files_[i].used) continue;

        /* Format: "NAME      SIZE\n" */
        int j = 0;
        while (files_[i].name[j] && pos < (int)max - 1) {
            out[pos++] = files_[i].name[j++];
        }
        /* Pad to 12 chars */
        while (j < 12 && pos < (int)max - 1) {
            out[pos++] = ' ';
            j++;
        }

        /* Size as decimal */
        u32 sz = files_[i].size;
        char num[11];
        int nd = 0;
        if (sz == 0) { num[0] = '0'; nd = 1; }
        else {
            while (sz > 0 && nd < 10) {
                num[nd++] = '0' + (sz % 10);
                sz /= 10;
            }
        }
        for (int k = nd - 1; k >= 0 && pos < (int)max - 1; k--)
            out[pos++] = num[k];

        if (pos < (int)max - 1) out[pos++] = '\n';
    }
    out[pos] = 0;
    return pos;
}
