"""XEKernelOS - QEMU launcher (floppy boot)"""
import os, sys, subprocess, shutil

BASE = os.path.dirname(os.path.abspath(__file__))
BLD  = os.path.join(BASE, 'build')

def build():
    if os.path.exists(BLD):
        print("Cleaning old build...")
        shutil.rmtree(BLD)
    print("Building...")
    r = subprocess.run(['make'], cwd=BASE)
    if r.returncode != 0:
        print("Build failed!")
        sys.exit(1)

def ensure_disk():
    path = os.path.join(BLD, 'disk.img')
    print("Building FAT12 disk...")
    import importlib.util
    mkdisk = os.path.join(BASE, 'tools', 'mkdisk.py')
    spec = importlib.util.spec_from_file_location("mkdisk", mkdisk)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.build_disk(path)

    # Inject CJK font at LBA 2048
    font_bin = os.path.join(BLD, 'font_cn.bin')
    if os.path.exists(font_bin):
        font_data = open(font_bin, 'rb').read()
        with open(path, 'r+b') as f:
            f.seek(2048 * 512)
            f.write(font_data)
        print(f"Font injected: {len(font_data)} bytes at LBA 2048")
    else:
        print("Warning: font_cn.bin not found, skipping")

def main():
    build()
    ensure_disk()

    cmd = [
        'qemu-system-i386',
        '-hda', os.path.join(BLD, 'xekernelos.img'),
        '-hdb', os.path.join(BLD, 'disk.img'),
        '-m', '32',
        '-boot', 'order=c',
        '-serial', 'stdio',
    ]
    print("XEKernelOS - Starting QEMU (hard disk boot)...")
    subprocess.call(cmd)

if __name__ == '__main__':
    main()
