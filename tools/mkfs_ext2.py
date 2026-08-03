#!/usr/bin/env python3
"""mkfs_ext2.py — 生成最小 ext2 文件系统镜像

输出: ext2.img (512KB, 1KB 块, 256 inode, 只读测试用)
这个镜像会被嵌入 disk.img 的 LBA 4096 处
"""

import struct
import os

BLOCK_SIZE = 1024
TOTAL_BLOCKS = 512  # 512KB
INODE_COUNT = 64
INODE_SIZE = 128
BLOCKS_PER_GROUP = 8192
INODES_PER_GROUP = INODE_COUNT  # all inodes in one group
ROOT_INO = 2

# ===== 块布局 =====
# Block 0: 引导块 (保留)
# Block 1: 超级块
# Block 2: 块组描述符表
# Block 3: 块位图
# Block 4: inode 位图
# Blocks 5-12: inode 表 (64 * 128 = 8192 bytes = 8 blocks)
# Blocks 13+: 数据块

BOOT_BLOCK      = 0
SUPERBLOCK_BLOCK = 1
BGD_BLOCK       = 2
BLOCK_BITMAP    = 3
INODE_BITMAP    = 4
INODE_TABLE     = 5
INODE_TABLE_BLOCKS = (INODE_COUNT * INODE_SIZE + BLOCK_SIZE - 1) // BLOCK_SIZE
FIRST_DATA_BLOCK = INODE_TABLE + INODE_TABLE_BLOCKS  # 13

# 预占块数 (超级块到 inode 表)
RESERVED_BLOCKS = FIRST_DATA_BLOCK  # 0-12 共 13 个块


def create_ext2_image():
    img = bytearray(TOTAL_BLOCKS * BLOCK_SIZE)

    # ---- 1. 超级块 (写入块 1, 偏移 0) ----
    sb_offset = SUPERBLOCK_BLOCK * BLOCK_SIZE
    free_blocks = TOTAL_BLOCKS - RESERVED_BLOCKS
    free_inodes = INODE_COUNT - 2  # inode 1 reserved, inode 2 root

    # struct ext2_superblock (packed)
    sb = struct.pack('<'
        'I'  # s_inodes_count
        'I'  # s_blocks_count
        'I'  # s_r_blocks_count
        'I'  # s_free_blocks_count
        'I'  # s_free_inodes_count
        'I'  # s_first_data_block
        'I'  # s_log_block_size (0 = 1024)
        'I'  # s_log_frag_size
        'I'  # s_blocks_per_group
        'I'  # s_frags_per_group
        'I'  # s_inodes_per_group
        'I'  # s_mtime
        'I'  # s_wtime
        'H'  # s_mnt_count
        'H'  # s_max_mnt_count
        'H'  # s_magic
        'H'  # s_state (1 = clean)
        'H'  # s_errors
        'H'  # s_minor_rev_level
        'I'  # s_lastcheck
        'I'  # s_checkinterval
        'I'  # s_creator_os
        'I'  # s_rev_level (1 = dynamic)
        'H'  # s_def_resuid
        'H'  # s_def_resgid
        # -- ext2 dynamic --
        'I'  # s_first_ino (11 for dynamic rev)
        'H'  # s_inode_size (128)
        'H'  # s_block_group_nr
        'I'  # s_feature_compat
        'I'  # s_feature_incompat (0x2 = filetype)
        'I'  # s_feature_ro_compat
        '16s'# s_uuid
        '16s'# s_volume_name ("XEKernel-ext2")
        '64s'# s_last_mounted
        'I'  # s_algo_bitmap
        'B'  # s_prealloc_blocks
        'B'  # s_prealloc_dir_blocks
        'H'  # _alignment
    ,
        INODE_COUNT,           # s_inodes_count
        TOTAL_BLOCKS,          # s_blocks_count
        0,                     # s_r_blocks_count
        free_blocks,           # s_free_blocks_count
        free_inodes,           # s_free_inodes_count
        1,                     # s_first_data_block (1 for 1KB blocks)
        0,                     # s_log_block_size (0 = 1024)
        0,                     # s_log_frag_size
        BLOCKS_PER_GROUP,      # s_blocks_per_group
        BLOCKS_PER_GROUP,      # s_frags_per_group
        INODES_PER_GROUP,      # s_inodes_per_group
        0,                     # s_mtime
        0,                     # s_wtime
        0,                     # s_mnt_count
        0xFFFF,                # s_max_mnt_count (no limit)
        0xEF53,                # s_magic
        1,                     # s_state (clean)
        1,                     # s_errors (continue)
        0,                     # s_minor_rev_level
        0,                     # s_lastcheck
        0,                     # s_checkinterval
        0,                     # s_creator_os (Linux)
        1,                     # s_rev_level (dynamic)
        0,                     # s_def_resuid
        0,                     # s_def_resgid
        11,                    # s_first_ino
        128,                   # s_inode_size
        0,                     # s_block_group_nr
        0,                     # s_feature_compat
        0x0002,                # s_feature_incompat (filetype)
        0,                     # s_feature_ro_compat
        b'\x00' * 16,          # s_uuid
        b'XEKernel-ext2\x00\x00\x00',  # s_volume_name (16 bytes)
        b'\x00' * 64,          # s_last_mounted
        0,                     # s_algo_bitmap
        0,                     # s_prealloc_blocks
        0,                     # s_prealloc_dir_blocks
        0,                     # _alignment
    )
    img[sb_offset:sb_offset + len(sb)] = sb

    # ---- 2. 块组描述符 (块 2) ----
    bgd_offset = BGD_BLOCK * BLOCK_SIZE
    bgd = struct.pack('<'
        'I'   # bg_block_bitmap
        'I'   # bg_inode_bitmap
        'I'   # bg_inode_table
        'H'   # bg_free_blocks_count
        'H'   # bg_free_inodes_count
        'H'   # bg_used_dirs_count (1 = root)
        'H'   # bg_pad
        '12s' # bg_reserved
    ,
        BLOCK_BITMAP,           # bg_block_bitmap
        INODE_BITMAP,           # bg_inode_bitmap
        INODE_TABLE,            # bg_inode_table
        free_blocks,            # bg_free_blocks_count
        free_inodes,            # bg_free_inodes_count
        1,                      # bg_used_dirs_count
        0,                      # bg_pad
        b'\x00' * 12,           # bg_reserved
    )
    img[bgd_offset:bgd_offset + len(bgd)] = bgd

    # ---- 3. 块位图 (块 3) ----
    bb_offset = BLOCK_BITMAP * BLOCK_SIZE
    # 标记预占块 (0-12) 为已使用
    for b in range(RESERVED_BLOCKS):
        byte_idx = b // 8
        bit_idx = b % 8
        img[bb_offset + byte_idx] |= (1 << bit_idx)

    # ---- 4. inode 位图 (块 4) ----
    ib_offset = INODE_BITMAP * BLOCK_SIZE
    # inode 1 和 2 已使用 (root)
    img[ib_offset] = 0x03  # bits 0,1 set

    # ---- 5. Inode 表 (块 5-12) ----
    it_offset = INODE_TABLE * BLOCK_SIZE

    def write_inode(ino, mode, size, blocks_list):
        """写入一个 inode, blocks_list 是数据块号列表 (最多 12 个直接块)"""
        idx = ino - 1  # 0-based
        off = it_offset + idx * INODE_SIZE

        i_block = [0] * 15
        for i, b in enumerate(blocks_list):
            if i < 15:
                i_block[i] = b

        sector_count = (len(blocks_list) * BLOCK_SIZE + 511) // 512

        raw = struct.pack('<'
            'H'    # i_mode
            'H'    # i_uid
            'I'    # i_size
            'I'    # i_atime
            'I'    # i_ctime
            'I'    # i_mtime
            'I'    # i_dtime
            'H'    # i_gid
            'H'    # i_links_count
            'I'    # i_blocks (512B sectors)
            'I'    # i_flags
            'I'    # i_osd1
            '15I'  # i_block[0..14]
            'I'    # i_generation
            'I'    # i_file_acl
            'I'    # i_dir_acl / i_size_high
            'I'    # i_faddr
            '12s'  # i_osd2
        ,
            mode,                  # i_mode
            0,                     # i_uid
            size,                  # i_size
            0, 0, 0,              # times
            0,                     # i_dtime
            0,                     # i_gid
            2 if ino == ROOT_INO else 1,  # i_links_count
            sector_count,          # i_blocks
            0,                     # i_flags
            0,                     # i_osd1
            i_block[0], i_block[1], i_block[2], i_block[3],
            i_block[4], i_block[5], i_block[6], i_block[7],
            i_block[8], i_block[9], i_block[10], i_block[11],
            i_block[12], i_block[13], i_block[14],
            0,                     # i_generation
            0,                     # i_file_acl
            0,                     # i_dir_acl
            0,                     # i_faddr
            b'\x00' * 12,          # i_osd2
        )
        img[off:off + len(raw)] = raw

        # 标记 inode 位图
        byte_idx = (ino - 1) // 8
        bit_idx = (ino - 1) % 8
        img[ib_offset + byte_idx] |= (1 << bit_idx)

    def alloc_block():
        """简单分配器: 累加计数器"""
        nonlocal next_free_block
        blk = next_free_block
        next_free_block += 1
        # 标记块位图
        byte_idx = blk // 8
        bit_idx = blk % 8
        img[bb_offset + byte_idx] |= (1 << bit_idx)
        return blk

    def create_dir_entry(ino, name, file_type):
        """创建目录项 (raw bytes)"""
        name_bytes = name.encode('ascii')
        name_len = len(name_bytes)
        # rec_len 按 4 字节对齐
        base_size = 8 + name_len
        rec_len = (base_size + 3) & ~3
        if rec_len < 12:
            rec_len = 12
        return struct.pack('<IHBB',
            ino, rec_len, name_len, file_type) + name_bytes + b'\x00' * (rec_len - 8 - name_len)

    def write_dir_data(block_num, entries):
        """将目录项写入数据块, 最后一项 rec_len 填满块"""
        data = bytearray()
        for i, entry in enumerate(entries):
            if i == len(entries) - 1:
                # 最后一项: rec_len 扩展到���末尾
                remaining = BLOCK_SIZE - len(data)
                ino = struct.unpack('<I', entry[:4])[0]
                nl = entry[6]
                ft = entry[7]
                name = entry[8:8+nl]
                # 重建带满 rec_len 的条目
                data += struct.pack('<IHBB', ino, remaining, nl, ft) + name + b'\x00' * (remaining - 8 - nl)
            else:
                data += entry
        img[block_num * BLOCK_SIZE:block_num * BLOCK_SIZE + len(data)] = data

    # ---- 分配数据块 ----
    next_free_block = FIRST_DATA_BLOCK

    # 预建测试数据
    readme_data = b"Welcome to ext2 on XEKernelOS!\nThis is a read-only ext2 driver.\nHave fun exploring the file system!\n"
    hello_data = b"Hello World from ext2!\n"
    config_data = b"# XEKernelOS ext2 config\nmax_files=64\nblock_size=1024\nreadonly=true\n"
    doc_data = b"ext2 quick reference:\n  - Superblock at block 1\n  - Inode size: 128 bytes\n  - Direct blocks: 0-11\n  - Indirect: 12\n"

    # ROOT 目录块: . 和 ..
    root_block = alloc_block()
    root_entries = [
        create_dir_entry(ROOT_INO, '.', 2),   # .
        create_dir_entry(ROOT_INO, '..', 2),  # ..
        # 文件条目在下面添加
    ]

    # 创建文件 inode
    inode_next = 3  # inode 1 reserved, 2 root

    # README.txt
    rblk = alloc_block()
    write_inode(inode_next, 0x81A4, len(readme_data), [rblk])  # 0x81A4 = regular file 0644
    img[rblk * BLOCK_SIZE:rblk * BLOCK_SIZE + len(readme_data)] = readme_data
    root_entries.append(create_dir_entry(inode_next, 'README.txt', 1))
    inode_next += 1

    # hello.txt
    hblk = alloc_block()
    write_inode(inode_next, 0x81A4, len(hello_data), [hblk])
    img[hblk * BLOCK_SIZE:hblk * BLOCK_SIZE + len(hello_data)] = hello_data
    root_entries.append(create_dir_entry(inode_next, 'hello.txt', 1))
    inode_next += 1

    # config.cfg
    cblk = alloc_block()
    write_inode(inode_next, 0x81A4, len(config_data), [cblk])
    img[cblk * BLOCK_SIZE:cblk * BLOCK_SIZE + len(config_data)] = config_data
    root_entries.append(create_dir_entry(inode_next, 'config.cfg', 1))
    inode_next += 1

    # docs/ 子目录
    docs_ino = inode_next
    docs_blk = alloc_block()
    write_inode(docs_ino, 0x41ED, BLOCK_SIZE, [docs_blk])  # 0x41ED = directory 0755
    docs_entries = [
        create_dir_entry(docs_ino, '.', 2),
        create_dir_entry(ROOT_INO, '..', 2),
    ]
    root_entries.append(create_dir_entry(docs_ino, 'docs', 2))
    inode_next += 1

    # docs/ref.txt
    ref_blk = alloc_block()
    write_inode(inode_next, 0x81A4, len(doc_data), [ref_blk])
    img[ref_blk * BLOCK_SIZE:ref_blk * BLOCK_SIZE + len(doc_data)] = doc_data
    docs_entries.append(create_dir_entry(inode_next, 'ref.txt', 1))
    inode_next += 1

    # LOST+FOUND 目录 (ext2 传统)
    lf_ino = inode_next
    lf_blk = alloc_block()
    write_inode(lf_ino, 0x41C0, BLOCK_SIZE, [lf_blk])  # 0x41C0 = directory 0700
    lf_entries = [
        create_dir_entry(lf_ino, '.', 2),
        create_dir_entry(ROOT_INO, '..', 2),
    ]
    root_entries.append(create_dir_entry(lf_ino, 'lost+found', 2))
    inode_next += 1

    # 写入目录数据
    write_dir_data(root_block, root_entries)
    write_dir_data(docs_blk, docs_entries)
    write_dir_data(lf_blk, lf_entries)

    # 写根 inode (必须最后写, 等 root_entries 完整)
    write_inode(ROOT_INO, 0x41ED, BLOCK_SIZE, [root_block])

    # 更新 BGD 空闲计数
    used_blocks = next_free_block - FIRST_DATA_BLOCK + RESERVED_BLOCKS
    free_b = TOTAL_BLOCKS - used_blocks
    used_inodes = inode_next - 1
    free_i = INODE_COUNT - used_inodes
    used_dirs = 3  # root, docs, lost+found

    struct.pack_into('<HHH', img, bgd_offset + 12,
        free_b, free_i, used_dirs)

    # 更新超级块空闲计数
    struct.pack_into('<II', img, sb_offset + 12,
        free_b, free_i)

    return bytes(img)


def main():
    img = create_ext2_image()

    output_path = os.path.join(os.path.dirname(__file__), '..', 'build', 'ext2.img')
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, 'wb') as f:
        f.write(img)

    size_kb = len(img) // 1024
    print(f"ext2.img: {size_kb}KB ({len(img)} bytes)")
    print(f"  blocks: {TOTAL_BLOCKS} x {BLOCK_SIZE}B")
    print(f"  inodes: {INODE_COUNT}")
    print(f"  files:  README.txt, hello.txt, config.cfg, docs/ref.txt")


if __name__ == '__main__':
    main()
