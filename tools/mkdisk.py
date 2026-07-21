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
    print(f"Warning: {hello_bin} not found, skipping")


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

    for name_str, content_bytes in DEMO_FILES:
        name_bytes = name_to_83(name_str)
        size = len(content_bytes)

        # Write file data
        data_offset = (DATA_SEC + (current_cluster - 2) * SPC) * BPS
        img[data_offset: data_offset + size] = content_bytes

        # Mark cluster as end-of-chain in FAT
        fat_entry_offset = current_cluster + current_cluster // 2
        fat_byte_offset = fat_entry_offset
        if current_cluster & 1:
            fat[fat_byte_offset] |= 0xF0
            fat[fat_byte_offset + 1] = 0xFF
        else:
            fat[fat_byte_offset] = 0xFF
            fat[fat_byte_offset + 1] = 0x0F

        # Also update fat2
        img[RESERVED * BPS + fat_byte_offset] = fat[fat_byte_offset]
        img[RESERVED * BPS + fat_byte_offset + 1] = fat[fat_byte_offset + 1]
        img[(RESERVED + FAT_SIZE) * BPS + fat_byte_offset] = fat[fat_byte_offset]
        img[(RESERVED + FAT_SIZE) * BPS + fat_byte_offset + 1] = fat[fat_byte_offset + 1]

        # Create root directory entry
        entry_idx = current_cluster - 2  # simple mapping
        entry_offset = entry_idx * 32
        entry = bytearray(32)
        entry[0:11] = name_bytes
        entry[11] = 0x20  # archive attribute
        struct.pack_into('<H', entry, 26, current_cluster)
        struct.pack_into('<I', entry, 28, size)
        root_dir[entry_offset: entry_offset + 32] = entry

        current_cluster += 1

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
