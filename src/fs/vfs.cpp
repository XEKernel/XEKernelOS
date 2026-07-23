#include "fs/vfs.h"

/* Mount table */
static mount_entry g_mounts[VFS_MAX_MOUNTS];
static int g_num_mounts = 0;

void vfs_init() {
    g_num_mounts = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].fs = nullptr;
        g_mounts[i].mount_point[0] = '\0';
    }
}

int vfs_mount(const char *mount_point, Filesystem *fs) {
    if (!mount_point || !fs) return -1;
    if (g_num_mounts >= VFS_MAX_MOUNTS) return -1;

    mount_entry *m = &g_mounts[g_num_mounts];
    int i = 0;
    while (mount_point[i] && i < 15) { m->mount_point[i] = mount_point[i]; i++; }
    m->mount_point[i] = '\0';
    m->fs = fs;
    g_num_mounts++;
    return 0;
}

Filesystem *vfs_resolve(const char *path, char *relative) {
    if (!path || !relative) return nullptr;

    /* If path doesn't start with '/', treat relative to root mount */
    const char *lookup = path;
    if (*lookup != '/') {
        /* Find root ("/") mount */
        for (int i = 0; i < g_num_mounts; i++) {
            if (g_mounts[i].mount_point[0] == '/' && g_mounts[i].mount_point[1] == '\0') {
                /* Copy full path as relative */
                int k = 0;
                while (lookup[k] && k < 127) { relative[k] = lookup[k]; k++; }
                relative[k] = '\0';
                return g_mounts[i].fs;
            }
        }
        return nullptr;
    }

    /* Find best (longest-prefix) mount match */
    Filesystem *best_fs = nullptr;
    int best_len = 0;

    for (int i = 0; i < g_num_mounts; i++) {
        const char *mp = g_mounts[i].mount_point;
        int j = 0;
        while (mp[j] && path[j] && mp[j] == path[j]) j++;
        if (mp[j] == '\0') {
            /* mount point matches prefix of path */
            if (j > best_len) {
                best_len = j;
                best_fs = g_mounts[i].fs;
            }
        }
    }

    if (!best_fs) return nullptr;

    /* Strip mount-point prefix → relative path */
    const char *rel = path + best_len;
    /* Handle "//" → skip extra slashes */
    while (*rel == '/') rel++;
    if (*rel == '\0') {
        /* Path IS the mount point — treat as root lookup */
        /* For root, we pass "." or empty string. Let's use "." */
        relative[0] = '\0';
    } else {
        int k = 0;
        while (rel[k] && k < 127) { relative[k] = rel[k]; k++; }
        relative[k] = '\0';
    }

    return best_fs;
}

/* ---- Dispatch wrappers ---- */

int vfs_open(const char *path, u8 *buf, u32 max_sz) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->open(rel, buf, max_sz);
}

int vfs_write(const char *path, const u8 *buf, u32 sz) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->write(rel, buf, sz);
}

int vfs_remove(const char *path) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->remove(rel);
}

int vfs_stat(const char *path, int *is_dir) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->stat(rel, is_dir);
}

int vfs_mkdir(const char *path) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->mkdir(rel);
}

int vfs_rmdir(const char *path) {
    char rel[128];
    Filesystem *fs = vfs_resolve(path, rel);
    if (!fs) return -1;
    return fs->rmdir(rel);
}

int vfs_rename(const char *old_path, const char *new_path) {
    char rel_old[128], rel_new[128];
    Filesystem *fs = vfs_resolve(old_path, rel_old);
    if (!fs) return -1;
    /* new_path must resolve to same FS */
    Filesystem *fs2 = vfs_resolve(new_path, rel_new);
    if (fs != fs2) return -1;
    return fs->rename(rel_old, rel_new);
}
