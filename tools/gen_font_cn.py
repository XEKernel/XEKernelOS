"""Generate Chinese font header from GNU Unifont hex data.
Extracts only glyphs needed by XEKernelOS system strings."""

import os
import sys
import re
import gzip
import urllib.request

# ─── All translatable system strings ─────────────────────────────

STRINGS = [
    # kernel.c
    "XEKernelOS v0.2.0 | x86 保护模式",
    "----------------------------------------",
    "PIC 重映射 (0x20/0x28)",
    "IDT + ISR 分发器",
    "PIT 100Hz | 键盘中断 | 鼠标中断",
    "内存初始化: %d 页",
    "ATA: %s",
    "ATA: 无磁盘",
    "FAT12 文件系统就绪",
    "FAT12: 无文件系统",
    "堆测试通过",
    "堆测试失败",
    "链表测试通过",
    "链表测试失败",

    # shell.c
    "XEKernel",  # prompt prefix
    "可用命令:",
    "HELP     - 显示帮助",
    "CLEAR    - 清屏",
    "INFO     - 系统信息",
    "MEM      - 内存统计",
    "DISK ID  - 磁盘识别",
    "DISK R   - 十六进制查看扇区",
    "DISK W   - 写入字节",
    "LS       - 列出文件",
    "CAT      - 查看文件内容",
    "CD       - 切换目录",
    "MKDIR    - 创建目录",
    "RMDIR    - 删除空目录",
    "CP       - 复制文件",
    "MV       - 重命名文件",
    "RM       - 删除文件",
    "CREATE   - 创建文件",
    "MOUSE    - 显示鼠标坐标",
    "RUN      - 加载运行程序",
    "ECHO     - 回显文本",
    "REBOOT   - 重启",
    "SHUTDOWN - 关机",

    # shell messages
    "未知命令。输入 HELP 查看帮助。",
    "未找到。",
    "不是目录。",
    "目录非空。",
    "磁盘已满。",
    "复制失败。",
    "源文件未找到。",
    "创建失败。",
    "已存在。",
    "文件未找到。",
    "加载程序失败。",
    "ATA 读取错误。",
    "ATA 写入错误。",
    "ATA 设备忙。",
    "ATAPI 设备（非硬盘）。",
    "ATA IDENTIFY 错误。",
    "偏移必须 < 512。",
    "重启中...",
    "已停机。",
    "已写入 0x%X 于 LBA %X+0x%X",
    "型号:",
    "序列号:",
    "扇区数:",
    "鼠标: X=%X Y=%X BTN=%X",
    "用法:",
    "Ring 3 用户模式",
    "架构: x86 32 位保护模式",
    "启动: MBR -> Stage2 -> 内核",
    "分页: 4KB 页 标识映射",
    "文件系统: FAT12 | 定时器: PIT 100Hz",
    "输入: 键盘中断 + PS/2 鼠标",
    "安全: Ring 3 + TSS",
    "空闲页: %d",

    # fat12.c
    "DIR",

    # kernel_boot prints (serial only, but add for completeness)
    "pic_remap",
    "idt_init",
    "mm_init",
    "gfx_init",
    "heap_init",
    "pit_init",
    "mouse_init",
    "kb_init",
    "user_init",
    "fat_init",
    "heap test",
    "list test",
]

# ─── Extract unique Chinese characters ───────────────────────────

def extract_chinese(text):
    """Extract unique CJK characters from text."""
    chars = set()
    for ch in text:
        cp = ord(ch)
        # CJK Unified Ideographs (U+4E00-U+9FFF)
        # CJK Extension A (U+3400-U+4DBF)
        # Fullwidth forms (U+FF00-U+FFEF)
        # CJK Symbols (U+3000-U+303F)
        if (0x4E00 <= cp <= 0x9FFF or
            0x3400 <= cp <= 0x4DBF or
            0xFF00 <= cp <= 0xFFEF or
            0x3000 <= cp <= 0x303F):
            chars.add(ch)
    return chars

all_chinese = set()
for s in STRINGS:
    all_chinese.update(extract_chinese(s))

# sort by codepoint for deterministic output
char_list = sorted(all_chinese, key=ord)
print(f"Total unique CJK characters needed: {len(char_list)}")
print("Characters:", "".join(char_list))

# ─── Download / parse Unifont hex ─────────────────────────────────

UNIFONT_URL = "https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont_jp-16.0.04.hex.gz"
# Fallback: plain text version
UNIFONT_URL_ALT = "https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont-16.0.04.hex.gz"

def download_unifont():
    """Download Unifont hex (gzipped) and parse needed glyphs."""
    url = UNIFONT_URL
    print(f"Downloading Unifont from {url} ...")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=60) as f:
            raw = f.read()
        data = gzip.decompress(raw).decode("utf-8", errors="ignore")
    except Exception as e:
        print(f"Primary URL failed ({e}), trying alt...")
        try:
            req = urllib.request.Request(UNIFONT_URL_ALT, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=60) as f:
                raw = f.read()
            data = gzip.decompress(raw).decode("utf-8", errors="ignore")
        except Exception as e2:
            print(f"Alt URL also failed: {e2}")
            return None

    print(f"Downloaded {len(data)} chars. Parsing...")
    glyphs = {}
    for line in data.splitlines():
        line = line.strip()
        if not line or ":" not in line:
            continue
        cp_str, hex_str = line.split(":", 1)
        try:
            cp = int(cp_str, 16)
        except ValueError:
            continue
        if cp in {ord(ch) for ch in char_list}:
            # Decode hex bitmap: 32 bytes for 16x16 = 64 hex chars
            hex_str = hex_str.strip()
            if len(hex_str) < 64:
                # Pad shorter glyphs (8-wide or 12-wide) to 16-wide
                hex_str = hex_str + "0" * (64 - len(hex_str))
            # Take only first 64 chars (32 bytes) for 16-wide
            hex_str = hex_str[:64]
            glyph_data = bytes.fromhex(hex_str) if len(hex_str) == 64 else b"\x00" * 32
            if len(glyph_data) == 32:
                glyphs[cp] = glyph_data

    print(f"Parsed {len(glyphs)} glyphs out of {len(char_list)} needed.")
    return glyphs


def generate_fallback_glyphs():
    """Generate minimal placeholder glyphs if Unifont unavailable.
    Each glyph is a 16x16 block with a simple outline."""
    print("Generating fallback glyphs (simple blocks)...")
    glyphs = {}
    for ch in char_list:
        cp = ord(ch)
        data = bytearray(32)
        # Draw a simple border + diagonal for visual distinction
        for row in range(16):
            byte0 = 0
            byte1 = 0
            if row == 0 or row == 15:
                byte0 = 0xFF
                byte1 = 0xFE  # full width minus rightmost 2px
            elif row < 8:
                byte0 = 0x80  # left border
                byte1 = 0x00
            else:
                byte0 = 0x80
                byte1 = (1 << (15 - row))  # diagonal
            data[row * 2] = byte0
            data[row * 2 + 1] = byte1
        glyphs[cp] = bytes(data)
    return glyphs


# ─── Main ─────────────────────────────────────────────────────────

def main():
    glyphs = download_unifont()
    if glyphs is None or len(glyphs) < len(char_list) * 0.5:
        print("Insufficient glyphs from Unifont, using fallback...")
        glyphs = generate_fallback_glyphs()

    # Build lookup table: array of {codepoint, offset}
    glyph_list = sorted(glyphs.items(), key=lambda x: x[0])

    out_path = os.path.join(os.path.dirname(__file__), "..", "src", "drivers", "font_cn.h")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("// Auto-generated Chinese font (GNU Unifont 16x16)\n")
        f.write(f"// Glyphs: {len(glyph_list)}\n")
        f.write("// clang-format off\n\n")
        f.write("#pragma once\n\n")

        # Glyph bitmap data (32 bytes each)
        f.write("static const unsigned char font_cn_data[] = {\n")
        for cp, data in glyph_list:
            hex_str = "".join(f"0x{b:02X}," for b in data)
            f.write(f"    // U+{cp:04X} {chr(cp)}\n")
            f.write(f"    {hex_str}\n")
        f.write("};\n\n")

        # Lookup table: array of {codepoint, data_offset}
        f.write("#define FONT_CN_COUNT  %d\n" % len(glyph_list))
        f.write("static const unsigned short font_cn_codepoint[] = {\n")
        for cp, _ in glyph_list:
            f.write(f"    0x{cp:04X},  // {chr(cp)}\n")
        f.write("};\n")

        print(f"Generated {out_path}")
        print(f"  {len(glyph_list)} glyphs, {len(glyph_list) * 32} bytes font data")


if __name__ == "__main__":
    main()
