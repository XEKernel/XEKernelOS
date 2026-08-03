"""Generate Chinese font header (font_cn.h) from PIL-rendered 16x16 bitmaps.

Scans source files for all Chinese characters, renders them with PIL,
and outputs a C header suitable for the XEKernelOS bitmap font system.
"""
import sys, os, re, struct
from PIL import Image, ImageDraw, ImageFont

SRC_DIR = "src"
FONT_H  = os.path.join(SRC_DIR, "drivers", "font_cn.h")
FONT_PATH = None  # auto-detect

def find_font():
    """Find a suitable Chinese TrueType font."""
    paths = [
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/Deng.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    ]
    for p in paths:
        if os.path.exists(p):
            return p
    return None

def render_char(char, font, size=16):
    """Render a single character as a 16x16 monochrome bitmap.
    Returns 32 bytes (2 bytes per row, MSB left)."""
    img = Image.new('L', (size, size), 0)
    draw = ImageDraw.Draw(img)
    # Center the character in the 16x16 cell
    bbox = draw.textbbox((0, 0), char, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (size - tw) // 2 - bbox[0]
    y = (size - th) // 2 - bbox[1]
    draw.text((x, y), char, fill=255, font=font)

    data = bytearray()
    for row in range(size):
        b0, b1 = 0, 0
        for col in range(8):
            px = img.getpixel((col, row))
            if px > 128:
                b0 |= (0x80 >> col)
        for col in range(8, 16):
            px = img.getpixel((col, row))
            if px > 128:
                b1 |= (0x80 >> (col - 8))
        data.append(b0)
        data.append(b1)
    return bytes(data)

def scan_chinese():
    """Scan all C/C++ source files for Chinese characters (Unicode > 0x7F)."""
    chars = set()
    utf8_re = re.compile(rb'[\xC0-\xDF][\x80-\xBF]|[\xE0-\xEF][\x80-\xBF]{2}|[\xF0-\xF7][\x80-\xBF]{3}')
    for root, dirs, files in os.walk(SRC_DIR):
        dirs[:] = [d for d in dirs if d not in ('.git', 'build')]
        for f in files:
            if f.endswith(('.cpp', '.h', '.c', '.asm')) and 'font_cn' not in f:
                path = os.path.join(root, f)
                with open(path, 'rb') as fh:
                    data = fh.read()
                for m in utf8_re.finditer(data):
                    try:
                        ch = m.group().decode('utf-8')
                        if len(ch) == 1 and (ord(ch) > 0x7F):
                            chars.add(ch)
                    except:
                        pass
    # Always include some essential CJK punctuation
    for c in '，。：；！？、…—《》【】「」『』（）':
        chars.add(c)
    return chars

def read_existing():
    """Read existing font_cn.h codepoints and bitmap data."""
    cps = {}
    if not os.path.exists(FONT_H):
        return cps
    with open(FONT_H, 'r', encoding='utf-8') as f:
        content = f.read()
    # Parse pairs of codepoint comments and data blocks
    pattern = re.compile(r'// U\+([0-9A-F]+).*?\n((?:\s*0x[0-9A-Fa-f]{2},){32})', re.MULTILINE)
    for m in pattern.finditer(content):
        cp = int(m.group(1), 16)
        data_str = m.group(2)
        bytes_list = [int(x.strip(), 16) for x in data_str.replace('\n', '').split(',') if x.strip()]
        if len(bytes_list) == 32:
            cps[cp] = bytes(bytes_list)
    return cps

def generate(chars, existing):
    """Generate font_cn.h content."""
    # Build sorted list of all codepoints with their data
    all_entries = []
    for ch in sorted(chars, key=lambda c: ord(c)):
        cp = ord(ch)
        if cp in existing:
            data = existing[cp]
        else:
            data = render_char(ch, g_font)
        all_entries.append((cp, ch, data))

    lines = []
    lines.append("// Auto-generated Chinese font (PIL-rendered 16x16)")
    lines.append(f"// Glyphs: {len(all_entries)}")
    lines.append("// clang-format off")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append("static const unsigned char font_cn_data[] = {")
    for cp, ch, data in all_entries:
        lines.append(f"    // U+{cp:04X} {ch}")
        hexes = ', '.join(f'0x{b:02X}' for b in data[:16])
        lines.append(f"    {hexes},")
        hexes = ', '.join(f'0x{b:02X}' for b in data[16:])
        lines.append(f"    {hexes},")
    lines.append("};")
    lines.append("")
    lines.append(f"#define FONT_CN_COUNT  {len(all_entries)}")
    lines.append("static const unsigned short font_cn_codepoint[] = {")
    for cp, ch, data in all_entries:
        lines.append(f"    0x{cp:04X},  // {ch}")
    lines.append("};")
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    font_path = find_font()
    if not font_path:
        print("ERROR: No Chinese font found. Install a CJK font.")
        sys.exit(1)
    print(f"Using font: {font_path}")

    # Load font at appropriate size. PIL renders at point size, 12pt ≈ 16px.
    g_font = ImageFont.truetype(font_path, 12)

    chars = scan_chinese()
    print(f"Scanned: {len(chars)} Chinese characters needed")

    existing = read_existing()
    print(f"Existing: {len(existing)} glyphs in font_cn.h")

    missing = [c for c in chars if ord(c) not in existing]
    print(f"New:      {len(missing)} characters to render")
    for c in missing:
        print(f"  U+{ord(c):04X} {c}")

    content = generate(chars, existing)
    with open(FONT_H, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Written:  {FONT_H} ({len(content)} bytes)")
