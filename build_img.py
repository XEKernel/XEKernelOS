"""XEKernelOS 磁盘镜像构建脚本"""
import sys, os, struct

bld = sys.argv[1] if len(sys.argv) > 1 else "build"
boot    = bytearray(open(os.path.join(bld, 'boot.bin'),    'rb').read())
stage2  = bytearray(open(os.path.join(bld, 'stage2.bin'),  'rb').read())
kernel  = open(os.path.join(bld, 'kernel.bin'),  'rb').read()

# Write kernel sector count at stage2 offset 0x1FFE (last 2 bytes of 8KB area)
ks = (len(kernel) + 511) // 512
struct.pack_into('<H', stage2, 0x1FFE, ks)

img = bytes(boot) + bytes(stage2) + kernel
img += b'\0' * (1474560 - len(img))

out = os.path.join(bld, 'xekernelos.img')
open(out, 'wb').write(img)
print(f'Written {len(img)}B (boot={len(boot)}, stage2={len(stage2)}, kernel={len(kernel)}, ksectors={ks})')
