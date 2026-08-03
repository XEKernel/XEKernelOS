"""Convert GNU Unifont .hex file to XEKernelOS font_cn.h, CJK subset."""

import gzip, sys, os

HEX_GZ = os.path.join(os.path.dirname(__file__), "unifont.hex.gz")
OUT    = os.path.join(os.path.dirname(__file__), "..", "src", "drivers", "font_cn.h")

# CJK ranges we want (common Chinese characters)
CJK_RANGES = [
    (0x3000, 0x303F),  # CJK Symbols and Punctuation
    (0x3400, 0x4DBF),  # CJK Extension A
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
    (0xFF00, 0xFFEF),  # Halfwidth and Fullwidth Forms
    (0x2000, 0x206F),  # General Punctuation
    (0xFE30, 0xFE4F),  # CJK Compatibility Forms
]

def in_range(cp):
    return any(lo <= cp <= hi for lo, hi in CJK_RANGES)

def parse_hex_line(line: str):
    """Parse 'U+XXXX:HH...' line. Returns (codepoint, 32 bytes) or None."""
    line = line.strip()
    if not line or line.startswith('#'):
        return None
    try:
        cp_str, data_str = line.split(':', 1)
        cp = int(cp_str, 16)
        if not in_range(cp):
            return None
        # HEX data: each byte is 2 hex chars. 32 bytes for 16x16 bitmap.
        data = bytes.fromhex(data_str)
        if len(data) != 32:
            return None
        return (cp, data)
    except:
        return None

def generate():
    """Read hex.gz, filter CJK, generate font_cn.h."""
    entries = []
    total = 0

    with gzip.open(HEX_GZ, 'rt', encoding='utf-8', errors='ignore') as f:
        for line in f:
            total += 1
            result = parse_hex_line(line)
            if result:
                entries.append(result)
            if total % 500000 == 0:
                print(f"  Scanned {total} lines, found {len(entries)} CJK chars...")

    print(f"Total: {total} lines scanned, {len(entries)} CJK glyphs found.")

    # Sort by codepoint
    entries.sort(key=lambda e: e[0])

    # Try to get a representative character for display in comments
    cp_to_char = {}
    for cp, data in entries:
        try:
            cp_to_char[cp] = chr(cp)
        except:
            cp_to_char[cp] = '?'

    # Write output
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write("// Auto-generated Chinese font (GNU Unifont 17.0, 16x16)\n")
        f.write(f"// Glyphs: {len(entries)}\n")
        f.write("// clang-format off\n\n")
        f.write("#pragma once\n\n")

        f.write("static const unsigned char font_cn_data[] = {\n")
        for cp, data in entries:
            ch = cp_to_char.get(cp, '?')
            f.write(f"    // U+{cp:04X} {ch}\n")
            hexes = ', '.join(f'0x{b:02X}' for b in data[:16])
            f.write(f"    {hexes},\n")
            hexes = ', '.join(f'0x{b:02X}' for b in data[16:])
            f.write(f"    {hexes},\n")
        f.write("};\n\n")

        f.write(f"#define FONT_CN_COUNT  {len(entries)}\n")
        f.write("static const unsigned short font_cn_codepoint[] = {\n")
        for cp, data in entries:
            ch = cp_to_char.get(cp, '?')
            f.write(f"    0x{cp:04X},  // {ch}\n")
        f.write("};\n")

    size = len(entries) * 32
    print(f"Written: {OUT} ({len(entries)} glyphs, {size//1024} KB data)")

if __name__ == '__main__':
    if not os.path.exists(HEX_GZ):
        print(f"ERROR: {HEX_GZ} not found. Run download first.")
        sys.exit(1)
    generate()
