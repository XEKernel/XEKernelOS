#!/usr/bin/env python3
"""把 raw 镜像转换成 VMware 可用的 vmdk, 并生成 .vmx 模板。

XEKernelOS 的启动链是 MBR + stage2 + 内核, 只用 BIOS 中断 (INT 13h / INT 10h
VBE / INT 15h E820), 因此 VMware 必须按 **BIOS 固件 + IDE 硬盘** 配置:

  IDE 0:0  启动盘 (xekernelos.img: MBR + stage2 + 内核)
  IDE 0:1  数据盘 (disk.img: FAT12 卷, 即用户可见的文件系统)

注意: SATA/NVMe 需要 AHCI 驱动 (内核未实现), EFI 需要 GOP 驱动 (未实现),
所以既不能用 SATA 也不能用 EFI 固件。

用法:
    python tools/mkvmdk.py                 # 输出到 build/vmware/ (默认补齐到 64MB)
    python tools/mkvmdk.py --pad-mb 0      # 不补齐, 按原始大小转换

补齐是非破坏性的: 会在 build/vmware/ 里生成补齐副本再转换, build/*.img 保持原样。
补齐的原因: 部分 BIOS/VMware 对小于 8MB 的 IDE "硬盘"识别异常 (不进启动流程)。
容量变化不影响功能 — 内核只按 BPB 里声明的 2880 扇区使用 FAT 卷。
"""
import argparse, os, shutil, subprocess, sys

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
OUT  = os.path.join(BLD, 'vmware')

BOOT_IMG = os.path.join(BLD, 'xekernelos.img')
DATA_IMG = os.path.join(BLD, 'disk.img')

VMX = r'''# XEKernelOS — VMware 虚拟机配置模板 (BIOS + IDE, 无需手工改)
# 生成者: tools/mkvmdk.py  |  说明: docs/VMWARE.md
.encoding = "windows-1252"
config.version = "8"
virtualHW.version = "12"       # 低版号兼容性更好 (VMware 12~17 均可直接打开)
displayName = "XEKernelOS"
guestOS = "other"
firmware = "bios"
memsize = "256"
numvcpus = "1"

# 内核要求 >= 31MB; 256MB 足够宽裕
# 两块 IDE 盘: 0:0 = 启动盘, 0:1 = 数据盘 (顺序不能颠倒)
ide0:0.present = "TRUE"
ide0:0.fileName = "XEKernelOS.vmdk"
ide0:0.deviceType = "disk"
ide0:1.present = "TRUE"
ide0:1.fileName = "XEKernelOS-data.vmdk"
ide0:1.deviceType = "disk"

# 串口 → 文件: 内核启动日志 (VBE 模式/内存大小/ATA 数据盘) 都从这里看
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "serial.log"
serial0.tryNoRxLoss = "FALSE"
serial0.yieldOnMsrRead = "TRUE"

floppy0.present = "FALSE"
ethernet0.present = "FALSE"     # 内核尚无网卡驱动
sound.present = "FALSE"
usb.present = "FALSE"

# 关 3D/加速, 保证走传统 VBE 线性帧缓冲 (内核只认 linear framebuffer)
mks.enable3d = "FALSE"
svga.autodetect = "TRUE"
svga.vramSize = "8388608"

bios.bootDelay = "3000"
powerType.powerOff = "soft"
powerType.reset = "soft"
tools.syncTime = "FALSE"
'''


def padded_copy(src, mb, dst):
    """把 src 复制成 dst 并补齐到 mb 兆字节 (源文件不动)"""
    size = os.path.getsize(src)
    want = mb * 1024 * 1024
    with open(src, 'rb') as fi, open(dst, 'wb') as fo:
        fo.write(fi.read())
        if want > size:
            fo.write(b'\x00' * (want - size))
    print(f'  {os.path.basename(src)} {size} B -> {os.path.basename(dst)} {max(size, want)} B')


def convert(src, dst):
    cmd = ['qemu-img', 'convert', '-f', 'raw', '-O', 'vmdk', src, dst]
    print('  ' + ' '.join(cmd))
    subprocess.run(cmd, check=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pad-mb', type=int, default=64,
                    help='补齐到该大小 (MB) 再转换, 0 = 按原始大小 (默认 64)')
    args = ap.parse_args()

    for p in (BOOT_IMG, DATA_IMG):
        if not os.path.exists(p):
            print(f'FAIL: 缺少 {p}, 先执行 make')
            return 1
    if not shutil.which('qemu-img'):
        print('FAIL: 找不到 qemu-img (MSYS2 clang64/bin 里, source tools/env.sh)')
        return 1

    os.makedirs(OUT, exist_ok=True)
    print('== 镜像准备与转换 ==')
    boot_src, data_src = BOOT_IMG, DATA_IMG
    if args.pad_mb:
        boot_src = os.path.join(OUT, '_boot.raw')
        data_src = os.path.join(OUT, '_data.raw')
        padded_copy(BOOT_IMG, args.pad_mb, boot_src)
        padded_copy(DATA_IMG, args.pad_mb, data_src)
    convert(boot_src, os.path.join(OUT, 'XEKernelOS.vmdk'))
    convert(data_src, os.path.join(OUT, 'XEKernelOS-data.vmdk'))
    for f in (boot_src, data_src):
        if f.startswith(OUT) and os.path.exists(f):
            os.remove(f)          # 中间补齐文件不留

    with open(os.path.join(OUT, 'XEKernelOS.vmx'), 'w', newline='\r\n') as f:
        f.write(VMX)

    print('\n== 完成 ==')
    for n in sorted(os.listdir(OUT)):
        print(f'  {n}  {os.path.getsize(os.path.join(OUT, n))} B')
    print('\n用 VMware 打开 build/vmware/XEKernelOS.vmx 即可; 排障看同目录 serial.log')
    return 0


if __name__ == '__main__':
    sys.exit(main())
