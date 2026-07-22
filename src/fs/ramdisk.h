/* ramdisk — in-memory file system mounted at /tmp
   Stores small files in a simple flat array. */

#pragma once
#include "lib/types.h"

#define RAMDISK_MAX_FILES 32
#define RAMDISK_MAX_SIZE  4096   /* max bytes per file */
#define RAMDISK_NAME_LEN  32

struct ramdisk_file {
    char name[RAMDISK_NAME_LEN];
    u8   data[RAMDISK_MAX_SIZE];
    u32  size;    /* actual bytes used */
    bool used;
};

class RamDisk {
public:
    void init();

    int  create(const char *name, const u8 *data, u32 size);
    int  read  (const char *name, u8 *out, u32 max);
    int  remove(const char *name);
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
