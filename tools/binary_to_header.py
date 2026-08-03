"""Convert a binary file to a C header with a byte array."""
import sys

if len(sys.argv) < 3:
    print("Usage: binary_to_header.py <input.bin> <varname>")
    sys.exit(1)

inpath = sys.argv[1]
varname = sys.argv[2]

data = open(inpath, 'rb').read()

print(f'/* Auto-generated from {inpath} — {len(data)} bytes */')
print(f'static const unsigned char {varname}[] = {{')
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hexes = ', '.join(f'0x{b:02X}' for b in chunk)
    print(f'    {hexes},')
print('};')
print(f'static const unsigned int {varname}_len = {len(data)};')
