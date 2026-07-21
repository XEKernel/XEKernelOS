"""Generate binary CJK font blob — linked into kernel via objcopy.
Format:
  [2 bytes]  count N (uint16 LE)
  [N * 34 bytes]  {uint16 cp, uint8 bitmap[32]}  sorted by codepoint
"""
import gzip, sys, os, struct

HEX_GZ = os.path.join(os.path.dirname(__file__), "unifont.hex.gz")
BIN    = os.path.join(os.path.dirname(__file__), "..", "build", "font_cn.bin")

CJK_RANGES = [
    (0x3000, 0x303F),  (0x3400, 0x4DBF),  (0x4E00, 0x9FFF),
    (0xFF00, 0xFFEF),  (0x2000, 0x206F),  (0xFE30, 0xFE4F),
]

def main():
    entries = []
    with gzip.open(HEX_GZ, 'rt', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            try:
                cp_str, data_str = line.split(':', 1)
                cp = int(cp_str, 16)
                if not any(lo <= cp <= hi for lo, hi in CJK_RANGES):
                    continue
                data = bytes.fromhex(data_str)
                if len(data) == 32:
                    entries.append((cp, data))
            except:
                pass

    entries.sort(key=lambda e: e[0])
    print(f"CJK glyphs: {len(entries)} ({len(entries)*32//1024} KB bitmap)")

    with open(BIN, 'wb') as f:
        f.write(struct.pack('<H', len(entries)))
        for cp, data in entries:
            f.write(struct.pack('<H', cp))
            f.write(data)

    print(f"Written: {BIN} ({os.path.getsize(BIN)} bytes)")

if __name__ == '__main__':
    main()
