#pragma once
#include "lib/types.h"
#include "fs/vfs.h"

/* ext2 文件系统常量 */
#define EXT2_MAGIC           0xEF53
#define EXT2_INODE_SIZE      128
#define EXT2_ROOT_INO        2

#define EXT2_S_IFMT          0xF000
#define EXT2_S_IFREG         0x8000
#define EXT2_S_IFDIR         0x4000

/* 目录项文件类型 (ext2 dir_entry file_type) */
#define EXT2_FT_UNKNOWN      0
#define EXT2_FT_REG_FILE     1
#define EXT2_FT_DIR          2

/* ext2 超级块 (磁盘结构, offset=1024 for 1KB blocks) */
struct __attribute__((packed)) ext2_sb {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    /* -- ext2 dynamic fields -- */
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8  s_uuid[16];
    u8  s_volume_name[16];
    u8  s_last_mounted[64];
    u32 s_algo_bitmap;
    /* performance hints */
    u8  s_prealloc_blocks;
    u8  s_prealloc_dir_blocks;
    u16 _alignment;
};

/* 块组描述符 (32 bytes on disk) */
struct __attribute__((packed)) ext2_bgd {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_used_dirs_count;
    u16 bg_pad;
    u8  bg_reserved[12];
};

/* Inode 结构 (128 bytes on disk) */
struct __attribute__((packed)) ext2_inode {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;       /* 512-byte sector count */
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15];    /* 0..11 direct, 12 indirect, 13 dindirect, 14 tindirect */
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;      /* or i_size_high */
    u32 i_faddr;
    u8  i_osd2[12];
};

/* 目录项 (variable length on disk) */
struct __attribute__((packed)) ext2_dir_entry {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[];
};


class Ext2Filesystem : public Filesystem {
public:
    int init(u32 base_lba);

    /* ---- VFS interface (准则一) ---- */
    int open(const char *path, u8 *buf, u32 max_sz) override;
    int stat(const char *path, int *is_dir) override;
    /* 只读: 写操作均返回 -1 */
    int write(const char *, const u8 *, u32) override { return -1; }
    int remove(const char *) override { return -1; }
    int mkdir(const char *) override { return -1; }
    int rmdir(const char *) override { return -1; }
    int rename(const char *, const char *) override { return -1; }

    /* 目录列表 (供 shell 使用, VFS override) */
    int dir(const char *path) override;

private:
    u32 base_lba_        = 0;
    u32 block_size_      = 0;
    u32 inodes_per_group_ = 0;
    u32 blocks_per_group_ = 0;
    u32 num_block_groups_  = 0;
    u32 first_data_block_  = 0;

    /* I/O helpers */
    int  read_block(u32 block, u8 *buf);
    int  read_inode(u32 ino, u8 *inode_buf);
    u32  get_data_block(const u8 *inode, u32 n);

    /* 路径解析 → inode 号; 失败返回 0 */
    u32  path_to_inode(const char *path);
};

extern Ext2Filesystem ext2;
