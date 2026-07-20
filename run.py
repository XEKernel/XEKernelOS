"""XEKernelOS - QEMU launcher"""
import os, sys, subprocess, struct

BASE = os.path.dirname(os.path.abspath(__file__))
BLD  = os.path.join(BASE, 'build')

def ensure_disk():
    path = os.path.join(BLD, 'disk.img')
    if os.path.exists(path):
        return path
    print("Creating disk.img ...")
    img = bytearray(1474560)
    img[0:3] = [0xEB, 0x3C, 0x90]
    img[3:11] = b'XEKERNEL'
    struct.pack_into('<H', img, 11, 512)
    img[13] = 1
    struct.pack_into('<H', img, 14, 1)
    img[16] = 2
    struct.pack_into('<H', img, 17, 224)
    struct.pack_into('<H', img, 19, 2880)
    img[21] = 0xF0
    struct.pack_into('<H', img, 22, 9)
    struct.pack_into('<H', img, 24, 18)
    struct.pack_into('<H', img, 26, 2)
    struct.pack_into('<I', img, 28, 0)
    img[32] = 0x29
    struct.pack_into('<I', img, 33, 0x20250720)
    img[38:49] = b'XEKernelOS '
    img[54:61] = b'FAT12   '
    img[512:515] = [0xF0, 0xFF, 0xFF]
    img[5120:5123] = [0xF0, 0xFF, 0xFF]
    with open(path, 'wb') as f:
        f.write(img)
    return path

def main():
    floppy = os.path.join(BLD, 'xekernelos.img')
    if not os.path.exists(floppy):
        print("Build not found. Run 'make' first.")
        sys.exit(1)
    disk = ensure_disk()
    cmd = [
        'qemu-system-i386',
        '-fda', floppy,
        '-hda', disk,
        '-m', '32',
        '-boot', 'order=a',
    ]
    print("XEKernelOS - Starting QEMU...")
    r = subprocess.call(cmd)
    if r != 0:
        print(f"QEMU exited with code {r}")
        input("Press Enter to continue...")

if __name__ == '__main__':
    main()
