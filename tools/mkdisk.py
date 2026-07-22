"""Build FAT12 disk image with demo files for XEKernelOS."""

import struct
import os

SECTOR_SIZE = 512
DISK_SIZE = 1474560  # 1.44 MB

# BPB fields (must match run.py)
BPS = 512
SPC = 1
RESERVED = 1
NUM_FAT = 2
ROOT_ENTS = 224
FAT_SIZE = 9
SECTORS_PER_TRACK = 18
NUM_HEADS = 2

ROOT_SEC = RESERVED + NUM_FAT * FAT_SIZE          # 19
DATA_SEC = ROOT_SEC + (ROOT_ENTS * 32 + BPS - 1) // BPS  # 33
ROOT_SECTORS = DATA_SEC - ROOT_SEC                   # 14

# Demo files: (8.3 name, content_bytes)
DEMO_FILES = []

def add_text(name_83, text):
    DEMO_FILES.append((name_83, text.encode('utf-8')))

def add_binary(name_83, path):
    with open(path, 'rb') as f:
        DEMO_FILES.append((name_83, f.read()))

add_text("README  TXT", "欢迎使用 XEKernelOS！\n\n这是一个示例文件。\n你可以用 CAT README.TXT 查看我。\n")
add_text("HELLO   TXT", "Hello from XEKernelOS!\n\n系统基于 x86 32 位保护模式。\n支持 FAT12 文件系统。\n")
add_text("DEMO    TXT", "XEKernelOS 演示文件\n==================\n\n创建文件: CREATE test.txt 内容\n查看文件: CAT test.txt\n删除文件: RM test.txt\n复制文件: CP a.txt b.txt\n重命名:   MV old.txt new.txt\n创建目录: MKDIR mydir\n删除目录: RMDIR mydir\n\n祝你使用愉快！\n")

# Binary user program
hello_bin = os.path.join(os.path.dirname(__file__), '..', 'build', 'hello.bin')
if os.path.exists(hello_bin):
    add_binary("HELLO   BIN", hello_bin)

# ELF test program
test_elf = os.path.join(os.path.dirname(__file__), '..', 'build', 'test_elf.elf')
if os.path.exists(test_elf):
    add_binary("TEST_ELFELF", test_elf)
else:
    print(f"Warning: {test_elf} not found, skipping")


def name_to_83(name_str):
    """Convert "README  TXT" to 11 bytes."""
    b = bytearray(11)
    for i in range(11):
        b[i] = ord(' ')
    for i, ch in enumerate(name_str):
        if i < 11:
            b[i] = ord(ch)
    return bytes(b)


def build_disk(output_path):
    img = bytearray(DISK_SIZE)

    # ---- BPB ----
    img[0:3] = b'\xEB\x3C\x90'
    img[3:11] = b'XEKERNEL'
    struct.pack_into('<H', img, 11, BPS)
    img[13] = SPC
    struct.pack_into('<H', img, 14, RESERVED)
    img[16] = NUM_FAT
    struct.pack_into('<H', img, 17, ROOT_ENTS)
    struct.pack_into('<H', img, 19, 2880)
    img[21] = 0xF0
    struct.pack_into('<H', img, 22, FAT_SIZE)
    struct.pack_into('<H', img, 24, SECTORS_PER_TRACK)
    struct.pack_into('<H', img, 26, NUM_HEADS)
    struct.pack_into('<I', img, 28, 0)
    img[32] = 0x29
    struct.pack_into('<I', img, 33, 0x20250720)
    img[38:49] = b'XEKernelOS '
    img[54:61] = b'FAT12   '

    # ---- FAT tables ----
    fat = bytearray(FAT_SIZE * BPS)
    fat[0:3] = b'\xF0\xFF\xFF'   # entries 0,1 reserved
    fat2 = bytearray(fat)
    img[RESERVED * BPS: RESERVED * BPS + len(fat)] = fat
    img[(RESERVED + FAT_SIZE) * BPS: (RESERVED + FAT_SIZE) * BPS + len(fat2)] = fat2

    # ---- Write files ----
    current_cluster = 2  # first data cluster
    root_dir = bytearray(ROOT_SECTORS * BPS)
    dir_entry_idx = 0  # separate counter for root directory entries

    def set_fat_entry(cl, value):
        """Write a 12-bit FAT entry for cluster cl."""
        off = cl + cl // 2
        if cl & 1:
            # Odd cluster: upper 12 bits of the 16-bit word
            old = fat[off]
            fat[off] = (old & 0x0F) | ((value & 0x0F) << 4)
            fat[off + 1] = (value >> 4) & 0xFF
        else:
            # Even cluster: lower 12 bits
            fat[off] = value & 0xFF
            fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)

    for name_str, content_bytes in DEMO_FILES:
        name_bytes = name_to_83(name_str)
        size = len(content_bytes)

        # Number of clusters needed
        cluster_size = SPC * BPS
        num_clusters = (size + cluster_size - 1) // cluster_size

        # Allocate cluster chain
        start_cluster = current_cluster
        prev_cluster = 0
        for i in range(num_clusters):
            cl = current_cluster
            if prev_cluster:
                set_fat_entry(prev_cluster, cl)
            prev_cluster = cl
            current_cluster += 1
        set_fat_entry(prev_cluster, 0xFFF)  # end of chain

        # Write file data across allocated clusters
        data_base = DATA_SEC * BPS
        for i, off in enumerate(range(0, size, cluster_size)):
            cl = start_cluster + i
            cl_off = (cl - 2) * cluster_size
            chunk = content_bytes[off: off + cluster_size]
            img[data_base + cl_off: data_base + cl_off + len(chunk)] = chunk

        # Create root directory entry
        entry_offset = dir_entry_idx * 32
        dir_entry_idx += 1
        entry = bytearray(32)
        entry[0:11] = name_bytes
        entry[11] = 0x20  # archive attribute
        struct.pack_into('<H', entry, 26, start_cluster)
        struct.pack_into('<I', entry, 28, size)
        root_dir[entry_offset: entry_offset + 32] = entry

    # Write root directory
    img[ROOT_SEC * BPS: ROOT_SEC * BPS + len(root_dir)] = root_dir
    # Write FATs back
    img[RESERVED * BPS: RESERVED * BPS + len(fat)] = fat
    img[(RESERVED + FAT_SIZE) * BPS: (RESERVED + FAT_SIZE) * BPS + len(fat2)] = fat

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(img)

    print(f"Built {output_path} ({len(DEMO_FILES)} files, {os.path.getsize(output_path)} bytes)")

    for name_str, content_bytes in DEMO_FILES:
        s = name_str.strip()
        name = s[:8].rstrip()
        ext = s[8:].rstrip() if len(s) > 8 else ''
        display = name + (('.' + ext) if ext else '')
        print(f"  {display:20s} {len(content_bytes)} bytes")


if __name__ == '__main__':
    out = os.path.join(os.path.dirname(__file__), '..', 'build', 'disk.img')
    build_disk(out)
