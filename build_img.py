"""XEKernelOS 磁盘镜像构建脚本"""
import sys, os

bld = sys.argv[1] if len(sys.argv) > 1 else "build"
boot    = open(os.path.join(bld, 'boot.bin'),    'rb').read()
stage2  = open(os.path.join(bld, 'stage2.bin'),  'rb').read()
kernel  = open(os.path.join(bld, 'kernel.bin'),  'rb').read()

img = boot + stage2 + kernel
img += b'\0' * (1474560 - len(img))

out = os.path.join(bld, 'xekernelos.img')
open(out, 'wb').write(img)
print(f'Written {len(img)}B (boot={len(boot)}, stage2={len(stage2)}, kernel={len(kernel)})')
