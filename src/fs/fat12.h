#pragma once
#include "lib/types.h"
#include "fs/vfs.h"

#define FAT12_BASE_LBA 2048

class FatFilesystem : public Filesystem {
public:
    int  init();
    int  dir();
    int  cd(const char *name);
    int  mkdir(const char *name) override;
    int  cat(const char *name);
    void cwd_str(char *out, int max);

    int  open(const char *name, u8 *out, u32 max_len) override;
    int  write(const char *name, const u8 *data, u32 size) override;
    int  remove(const char *name) override;
    int  stat(const char *name, int *is_dir) override;
    int  rmdir(const char *name) override;
    int  rename(const char *old_name, const char *new_name) override;
    u16  find_free_cluster();
    u16  alloc_cluster();
    void set_cluster(u16 cluster, u16 value);

private:
    u8   buf_[512]{};
    int  start_sec_ = 0, fat_secs_ = 0, root_secs_ = 0, data_sec_ = 0;
    u16  spc_ = 0, bps_ = 0;
    u8   num_fat_ = 0;
    u16  root_ents_ = 0;
    u16  cur_dir_cluster_ = 0;
    char cur_dir_name_[13]{};

    int  curdir_secs();
    int  read_curdir(int sec_idx);
    int  write_curdir(int sec_idx);
    u16  next_cluster(u16 cl);
    u16  read_fat_entry(u16 cl);
    void write_fat_entry(u16 cl, u16 value);
    int  read_root_sec(int sector, u8 *dst);
    int  write_root_sec(int sector, u8 *src);
    void name83_to_str(u8 *e, char *name);
    void str_to_name83(const char *name, u8 *fname);
};

extern FatFilesystem fat;

/* C-compat wrappers */
inline int  fat_init()                { return fat.init(); }
inline int  fat_dir()                 { return fat.dir(); }
inline int  fat_cd(const char *n)     { return fat.cd(n); }
inline int  fat_mkdir(const char *n)  { return fat.mkdir(n); }
inline int  fat_cat(const char *n)    { return fat.cat(n); }
inline void fat_cwd_str(char *o, int m){ fat.cwd_str(o, m); }
inline int  fat_read_file_buf(const char *n, u8 *o, u32 m) { return fat.open(n, o, m); }
inline int  fat_write_file(const char *n, const u8 *d, u32 s) { return fat.write(n, d, s); }
inline int  fat_delete_file(const char *n) { return fat.remove(n); }
inline int  fat_rmdir(const char *n)  { return fat.rmdir(n); }
inline int  fat_rename(const char *o, const char *n) { return fat.rename(o, n); }
inline int  fat_stat(const char *n, int *is_dir)  { return fat.stat(n, is_dir); }
inline u16  fat_find_free_cluster()   { return fat.find_free_cluster(); }
inline u16  fat_alloc_cluster()       { return fat.alloc_cluster(); }
inline void fat_set_cluster(u16 c, u16 v) { fat.set_cluster(c, v); }
