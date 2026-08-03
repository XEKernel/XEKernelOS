/* ramdisk — in-memory file system mounted at /tmp
   Stores small files in a simple flat array.
   Implements Filesystem interface for VFS integration. */

#pragma once
#include "lib/types.h"
#include "fs/vfs.h"

#define RAMDISK_MAX_FILES 32
#define RAMDISK_MAX_SIZE  4096   /* max bytes per file */
#define RAMDISK_NAME_LEN  32

struct ramdisk_file {
    char name[RAMDISK_NAME_LEN];
    u8   *data;   /* heap-allocated, nullptr if unused */
    u32  size;    /* actual bytes used */
    bool used;
};

class RamDisk : public Filesystem {
public:
    void init();

    /* ---- VFS interface ---- */
    int open(const char *path, u8 *buf, u32 max_sz) override;
    int write(const char *path, const u8 *data, u32 size) override;
    int remove(const char *path) override;
    int stat(const char *path, int *is_dir) override;
    int mkdir(const char *) override { return -1; }   /* flat FS */
    int rmdir(const char *) override { return -1; }
    int rename(const char *, const char *) override { return -1; }
    int dir(const char *path) override;

    /* ---- Legacy API (backward compat) ---- */
    int  create(const char *name, const u8 *data, u32 size);
    int  read  (const char *name, u8 *out, u32 max);
    int  list  (char *out, u32 max);  /* returns bytes written */

private:
    ramdisk_file files_[RAMDISK_MAX_FILES];
    int find_free();
    int find_name(const char *name);
};

extern RamDisk ramdisk;

inline void rd_init()                       { ramdisk.init(); }
inline int  rd_create(const char *n, const u8 *d, u32 s) { return ramdisk.create(n, d, s); }
inline int  rd_read(const char *n, u8 *o, u32 m)    { return ramdisk.read(n, o, m); }
inline int  rd_remove(const char *n)         { return ramdisk.remove(n); }
inline int  rd_list(char *o, u32 m)          { return ramdisk.list(o, m); }
