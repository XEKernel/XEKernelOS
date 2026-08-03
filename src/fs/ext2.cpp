/* ext2 只读文件系统驱动
 *
 * Phase 3 #5 — VFS 框架下的 ext2 只读支持
 * 准则一: 万物皆 fd — 实现 Filesystem 接口, 不引入专用 syscall
 *
 * 磁盘布局: ext2 分区位于 disk.img LBA 4096 处, 1KB 块大小
 */

#include "fs/ext2.h"
#include "drivers/bcache.h"
#include "drivers/gfx.h"
#include "drivers/serial.h"

Ext2Filesystem ext2;

/* ---- 初始化 ---- */

int Ext2Filesystem::init(u32 base_lba) {
    base_lba_ = base_lba;

    /* 读取超级块: 1KB 偏移 = LBA + 2 */
    u8 raw[1024];
    int r = bc_read(base_lba_ + 2, 2, raw);
    if (r < 0) {
        serial_write_str("ext2: superblock read failed\n");
        return -1;
    }

    ext2_sb *sb = (ext2_sb *)raw;

    /* 魔数校验 */
    if (sb->s_magic != EXT2_MAGIC) {
        serial_write_str("ext2: bad magic\n");
        return -2;
    }

    block_size_ = 1024u << sb->s_log_block_size;
    if (block_size_ > 4096) {
        serial_write_str("ext2: block size > 4KB unsupported\n");
        return -3;
    }
    inodes_per_group_ = sb->s_inodes_per_group;
    blocks_per_group_ = sb->s_blocks_per_group;
    first_data_block_ = sb->s_first_data_block;

    /* 块组数 = ceil(s_blocks_count / blocks_per_group) */
    num_block_groups_ = (sb->s_blocks_count + blocks_per_group_ - 1) / blocks_per_group_;

    serial_write_str("ext2: magic OK, block_size=");
    {
        u32 bs = block_size_;
        char tmp[8]; int n = 0;
        while (bs) { tmp[n++] = '0' + (bs % 10); bs /= 10; }
        for (int i = n - 1; i >= 0; i--) serial_write_char(tmp[i]);
    }
    serial_write_str(" inodes=");
    {
        u32 ic = sb->s_inodes_count;
        char tmp[8]; int n = 0;
        while (ic) { tmp[n++] = '0' + (ic % 10); ic /= 10; }
        for (int i = n - 1; i >= 0; i--) serial_write_char(tmp[i]);
    }
    serial_write_str(" blocks=");
    {
        u32 bc = sb->s_blocks_count;
        char tmp[8]; int n = 0;
        while (bc) { tmp[n++] = '0' + (bc % 10); bc /= 10; }
        for (int i = n - 1; i >= 0; i--) serial_write_char(tmp[i]);
    }
    serial_write_str("\n");

    return 0;
}


/* ---- 底层 I/O ---- */

int Ext2Filesystem::read_block(u32 block, u8 *buf) {
    /* 将逻辑块号转换为 LBA */
    u8 secs_per_block = block_size_ / 512;
    u32 lba = base_lba_ + block * secs_per_block;
    return bc_read(lba, secs_per_block, buf);
}

int Ext2Filesystem::read_inode(u32 ino, u8 *inode_buf) {
    if (ino < 1) return -1;

    u32 group = (ino - 1) / inodes_per_group_;
    u32 idx   = (ino - 1) % inodes_per_group_;

    if (group >= num_block_groups_) return -1;

    /* 读取块组描述符表: 紧接超级块
     * BGD 表所在块 = first_data_block_ + 1
     * 每个 BGD 32 字节 */
    u32 bgd_block = first_data_block_ + 1;
    u8  bgd_raw[1024];
    if (read_block(bgd_block, bgd_raw) < 0) return -1;

    /* 获取本块组的 BGD */
    ext2_bgd *bgd = (ext2_bgd *)(bgd_raw + group * 32);

    /* inode 表所在块 = bg_inode_table + (idx * EXT2_INODE_SIZE) / block_size_ */
    u32 itable_block = bgd->bg_inode_table;
    u32 inode_offset = idx * EXT2_INODE_SIZE;
    u32 block_offset = inode_offset / block_size_;

    u8 block_buf[4096];  /* 覆盖到 4KB 块 (与 init 的限制一致) */
    if (read_block(itable_block + block_offset, block_buf) < 0) return -1;

    u32 off_in_block = inode_offset % block_size_;

    /* 复制 inode 到调用者的缓冲区 */
    ext2_inode *src = (ext2_inode *)(block_buf + off_in_block);
    ext2_inode *dst = (ext2_inode *)inode_buf;
    for (u32 i = 0; i < EXT2_INODE_SIZE; i++)
        ((u8 *)dst)[i] = ((u8 *)src)[i];

    return 0;
}

/* 解析 inode 的第 n 个数据块 (n 从 0 开始) */
u32 Ext2Filesystem::get_data_block(const u8 *inode_raw, u32 n) {
    const ext2_inode *ino = (const ext2_inode *)inode_raw;
    u32 ptrs_per_block = block_size_ / 4;  /* 每块可容纳的指针数 */

    /* 直接块: 0..11 */
    if (n < 12)
        return ino->i_block[n];
    n -= 12;

    /* 单级间接块: i_block[12] */
    if (n < ptrs_per_block) {
        u32 indir_block = ino->i_block[12];
        if (indir_block == 0) return 0;
        u8 buf[4096];
        if (read_block(indir_block, buf) < 0) return 0;
        return ((u32 *)buf)[n];
    }
    n -= ptrs_per_block;

    /* 二级间接块: i_block[13] */
    if (n < ptrs_per_block * ptrs_per_block) {
        u32 dindir_block = ino->i_block[13];
        if (dindir_block == 0) return 0;

        u32 idx1 = n / ptrs_per_block;   /* 一级索引 */
        u32 idx2 = n % ptrs_per_block;   /* 二级索引 */

        u8 buf[4096];
        if (read_block(dindir_block, buf) < 0) return 0;
        u32 indir = ((u32 *)buf)[idx1];
        if (indir == 0) return 0;

        if (read_block(indir, buf) < 0) return 0;
        return ((u32 *)buf)[idx2];
    }
    n -= ptrs_per_block * ptrs_per_block;

    /* 三级间接块: i_block[14] (少量实现, 对大文件) */
    {
        u32 tindir_block = ino->i_block[14];
        if (tindir_block == 0) return 0;

        u32 idx1 = n / (ptrs_per_block * ptrs_per_block);
        u32 rem1 = n % (ptrs_per_block * ptrs_per_block);
        u32 idx2 = rem1 / ptrs_per_block;
        u32 idx3 = rem1 % ptrs_per_block;

        u8 buf[4096];
        if (read_block(tindir_block, buf) < 0) return 0;
        u32 dindir = ((u32 *)buf)[idx1];
        if (dindir == 0) return 0;

        if (read_block(dindir, buf) < 0) return 0;
        u32 indir = ((u32 *)buf)[idx2];
        if (indir == 0) return 0;

        if (read_block(indir, buf) < 0) return 0;
        return ((u32 *)buf)[idx3];
    }
}


/* ---- 路径解析 ---- */

u32 Ext2Filesystem::path_to_inode(const char *path) {
    if (!path || !*path) return 0;

    /* 跳过前导斜杠 */
    while (*path == '/') path++;
    if (*path == '\0') return EXT2_ROOT_INO;  /* "/" → root */

    u32 current_ino = EXT2_ROOT_INO;

    /* 逐级解析路径 */
    while (*path) {
        /* 提取下一个路径组件 */
        char comp[256];
        int clen = 0;
        while (*path && *path != '/' && clen < 255)
            comp[clen++] = *path++;
        comp[clen] = '\0';
        while (*path == '/') path++;

        /* 读取当前 inode */
        u8 inode_buf[128];
        if (read_inode(current_ino, inode_buf) < 0) return 0;
        ext2_inode *ino = (ext2_inode *)inode_buf;

        /* 必须是目录 */
        if ((ino->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return 0;

        /* 遍历目录项查找 comp */
        u32 found_ino = 0;
        u32 total_blocks = (ino->i_size + block_size_ - 1) / block_size_;

        for (u32 blk = 0; blk < total_blocks && found_ino == 0; blk++) {
            u32 db = get_data_block(inode_buf, blk);
            if (db == 0) continue;

            u8 dir_buf[4096];
            if (read_block(db, dir_buf) < 0) continue;

            u32 offset = 0;
            while (offset < block_size_) {
                ext2_dir_entry *de = (ext2_dir_entry *)(dir_buf + offset);
                if (de->inode == 0 || de->rec_len == 0) break;

                if (de->name_len == (u8)clen) {
                    int match = 1;
                    for (int k = 0; k < clen; k++)
                        if (de->name[k] != comp[k]) { match = 0; break; }
                    if (match) {
                        found_ino = de->inode;
                        break;
                    }
                }

                offset += de->rec_len;
            }
        }

        if (found_ino == 0) return 0;  /* 组件未找到 */
        current_ino = found_ino;
    }

    return current_ino;
}


/* ---- VFS 接口实现 ---- */

int Ext2Filesystem::open(const char *path, u8 *buf, u32 max_sz) {
    u32 ino = path_to_inode(path);
    if (ino == 0) return -1;

    u8 inode_buf[128];
    if (read_inode(ino, inode_buf) < 0) return -1;

    ext2_inode *ino_ptr = (ext2_inode *)inode_buf;

    /* 不是普通文件 */
    if ((ino_ptr->i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) return -1;

    u32 fsize = ino_ptr->i_size;
    u32 to_read = (fsize < max_sz) ? fsize : max_sz;
    u32 total_blocks = (fsize + block_size_ - 1) / block_size_;
    u32 copied = 0;

    for (u32 blk = 0; blk < total_blocks && copied < to_read; blk++) {
        u32 db = get_data_block(inode_buf, blk);
        if (db == 0) break;

        u8 block_buf[4096];
        if (read_block(db, block_buf) < 0) break;

        u32 chunk = block_size_;
        if (blk == total_blocks - 1)
            chunk = fsize - blk * block_size_;  /* 最后一块的剩余部分 */
        if (copied + chunk > to_read)
            chunk = to_read - copied;

        for (u32 i = 0; i < chunk; i++)
            buf[copied + i] = block_buf[i];
        copied += chunk;
    }

    return (int)fsize;  /* 返回实际文件大小 */
}

int Ext2Filesystem::stat(const char *path, int *is_dir) {
    u32 ino = path_to_inode(path);
    if (ino == 0) return -1;

    u8 inode_buf[128];
    if (read_inode(ino, inode_buf) < 0) return -1;

    ext2_inode *ino_ptr = (ext2_inode *)inode_buf;
    u16 mode = ino_ptr->i_mode;

    if (is_dir) *is_dir = ((mode & EXT2_S_IFMT) == EXT2_S_IFDIR) ? 1 : 0;

    return (int)(ino_ptr->i_size);
}


/* ---- 目录列表 (供 shell 使用) ---- */

int Ext2Filesystem::dir(const char *path) {
    u32 ino = path_to_inode(path);
    if (ino == 0) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts("ext2: path not found\n");
        gfx_set_fg(COLOR_LGRAY);
        return -1;
    }

    u8 inode_buf[128];
    if (read_inode(ino, inode_buf) < 0) return -1;

    ext2_inode *ino_ptr = (ext2_inode *)inode_buf;

    if ((ino_ptr->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
        gfx_set_fg(COLOR_LRED);
        gfx_puts("ext2: not a directory\n");
        gfx_set_fg(COLOR_LGRAY);
        return -1;
    }

    u32 total_blocks = (ino_ptr->i_size + block_size_ - 1) / block_size_;

    for (u32 blk = 0; blk < total_blocks; blk++) {
        u32 db = get_data_block(inode_buf, blk);
        if (db == 0) continue;

        u8 dir_buf[4096];
        if (read_block(db, dir_buf) < 0) continue;

        u32 offset = 0;
        while (offset < block_size_) {
            ext2_dir_entry *de = (ext2_dir_entry *)(dir_buf + offset);
            if (de->inode == 0 || de->rec_len == 0) break;

            /* 读取条目的 inode 获取文件大小 */
            u8 ent_inode[128];
            u32 fsize = 0;
            int is_dir = 0;
            if (read_inode(de->inode, ent_inode) == 0) {
                ext2_inode *ei = (ext2_inode *)ent_inode;
                fsize = ei->i_size;
                is_dir = ((ei->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR);
            }

            /* 颜色: 目录=青色, 文件=白色 */
            gfx_set_fg(is_dir ? COLOR_LCYAN : COLOR_WHITE);

            /* 大小 (灰色, 右对齐 9 位) */
            gfx_set_fg(COLOR_DGRAY);
            {
                char sz[10];
                for (int i = 0; i < 9; i++) sz[i] = ' ';
                int n = 0, t = fsize;
                if (t == 0) { sz[8] = '0'; n = 1; }
                else { while (t && n < 9) { sz[8 - n] = '0' + (t % 10); t /= 10; n++; } }
                for (int i = 0; i < 9; i++) gfx_putc(sz[i]);
            }
            gfx_putc(' ');

            /* 名称 */
            gfx_set_fg(is_dir ? COLOR_LCYAN : COLOR_WHITE);
            for (int k = 0; k < de->name_len; k++)
                gfx_putc(de->name[k]);
            gfx_putc('\n');

            offset += de->rec_len;
        }
    }

    gfx_set_fg(COLOR_LGRAY);
    return 0;
}
