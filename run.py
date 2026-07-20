"""XEKernelOS - QEMU launcher"""
import os, sys, subprocess, struct

BASE = os.path.dirname(os.path.abspath(__file__))
BLD  = os.path.join(BASE, 'build')

def ensure_disk():
    path = os.path.join(BLD, 'disk.img')
    if os.path.exists(path):
        return path
    print("Creating disk.img with demo files...")
    # Run mkdisk tool
    import importlib.util
    mkdisk = os.path.join(BASE, 'tools', 'mkdisk.py')
    spec = importlib.util.spec_from_file_location("mkdisk", mkdisk)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.build_disk(path)
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
        '-serial', 'stdio',
    ]
    print("XEKernelOS - Starting QEMU...")
    r = subprocess.call(cmd)
    if r != 0:
        print(f"QEMU exited with code {r}")
        input("Press Enter to continue...")

if __name__ == '__main__':
    main()
