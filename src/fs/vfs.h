#pragma once
#include "lib/types.h"

/* VFS — Virtual Filesystem abstraction (准则五: kernel-side FS multiplexer)
 *
 * Every mountable filesystem implements this interface.
 * The VFS layer routes path-based syscalls to the correct FS.
 */

#define VFS_MAX_MOUNTS 4

/* 目录项 (资源管理器用): is_dir=1 目录, size 文件大小, name 为短文件名 */
struct DirEntry {
    int  is_dir;
    u32  size;
    char name[32];
};

class Filesystem {
public:
    /* base class virtual destructor */
    virtual ~Filesystem() = default;

    /* Open & load entire file into caller-provided buffer.
       Returns file size on success, -1 on failure. */
    virtual int open(const char *path, u8 *buf, u32 max_sz) = 0;

    /* Write buffer to file on disk. Returns 0 on success, -1 on failure. */
    virtual int write(const char *path, const u8 *buf, u32 sz) = 0;

    /* Delete file. Returns 0 on success, -1 on failure. */
    virtual int remove(const char *path) = 0;

    /* Stat: returns file size, sets *is_dir to 1 if directory, 0 if file.
       Returns -1 on failure. */
    virtual int stat(const char *path, int *is_dir) = 0;

    /* Create directory. Returns 0 on success, -1 on failure. */
    virtual int mkdir(const char *path) = 0;

    /* Remove directory. Returns 0 on success, -1 on failure. */
    virtual int rmdir(const char *path) = 0;

    /* Rename/move. Returns 0 on success, -1 on failure. */
    virtual int rename(const char *old_path, const char *new_path) = 0;

    /* List directory contents to framebuffer.
       path is relative to FS root. Returns 0 on success, -1 on failure. */
    virtual int dir(const char *path) = 0;

    /* List directory entries into caller buffer (资源管理器用).
       Returns count of entries on success, -1 on failure. 默认不支持. */
    virtual int list(const char *path, DirEntry *e, u32 max) { (void)path; (void)e; (void)max; return -1; }
};

struct mount_entry {
    char        mount_point[16];  /* e.g. "/" */
    Filesystem *fs;
};

/* ---- VFS API ---- */

/* Register a filesystem at mount_point. Returns 0 on success. */
int  vfs_mount(const char *mount_point, Filesystem *fs);

/* Resolve path → (Filesystem*, relative_path).
   relative_path must be at least 128 bytes. Returns nullptr if no mount matches. */
Filesystem *vfs_resolve(const char *path, char *relative);

/* Dispatch wrappers — resolve path, call FS method. */
int  vfs_open(const char *path, u8 *buf, u32 max_sz);
int  vfs_write(const char *path, const u8 *buf, u32 sz);
int  vfs_remove(const char *path);
int  vfs_stat(const char *path, int *is_dir);
int  vfs_mkdir(const char *path);
int  vfs_rmdir(const char *path);
int  vfs_rename(const char *old_path, const char *new_path);
int  vfs_dir(const char *path);

/* List directory entries into structured buffer. Returns count or -1. */
int  vfs_list_dir(const char *path, DirEntry *entries, u32 max);

/* Init VFS layer (clears mount table). */
void vfs_init();
