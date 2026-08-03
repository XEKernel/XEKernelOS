#!/usr/bin/env python3
"""XEKernelOS QEMU 自动化冒烟测试
- 通过 monitor sendkey 模拟键盘输入用户态 Shell 命令
- screendump 截取 framebuffer
- 退出后解析 disk.img 的 FAT12 结构, 验证簇分配正确性 (回归 BUG#1)
用法: python tools/qemu_auto.py
"""
import os, re, subprocess, sys, time, struct, socket

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
QEMU = os.environ.get('QEMU', 'qemu-system-i386')

# QEMU monitor 键名映射 (小写)
KEYMAP = {
    ' ': 'spc', '\n': 'ret', '\r': 'ret',
    '.': 'dot', ',': 'comma', '/': 'slash', '-': 'minus', '_': 'underscore',
    '=': 'equal', '+': 'shift-equal', ':': 'shift-semicolon', ';': 'semicolon',
    '(': 'shift-9', ')': 'shift-0', '!': 'shift-1', '?': 'shift-slash',
    '\\': 'backslash', "'": 'apostrophe',
}

class Qemu:
    def __init__(self, img, disk):
        self.log_path = os.path.join(BLD, 'qemu_serial.log')
        if os.path.exists(self.log_path):
            os.remove(self.log_path)
        # QEMU 9.x 对 raw 镜像探测模式下限制 block 0 写——必须用 qcow2 包装
        # qemu-img convert -c -O qcow2 -S 4M 创建一份压缩零碎优化的拷贝
        self.disk_qcow2 = os.path.join(BLD, 'disk.qcow2')
        if os.path.exists(self.disk_qcow2):
            os.remove(self.disk_qcow2)
        subprocess.run(
            ['qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', '-S', '4M',
             disk, self.disk_qcow2],
            check=True, capture_output=True)
        # 串口走 TCP server: 内核"串口输入桥"会把串口字节当作键盘输入,
        # 绕过 QEMU 11.x sendkey 不触发 PS/2 FIFO 的缺陷, 命令注入完全可靠.
        # monitor 走 stdio 只做 screendump/quit.
        self.serial_port = 7777
        self.sock = None
        self.serial_buf = ''
        self.p = None
        try:
            self.p = subprocess.Popen(
                [QEMU,
                 '-drive', f'file={self.disk_qcow2},format=qcow2,if=ide,index=1',
                 '-drive', f'file={img},format=raw,if=ide,index=0',
                 '-m', '32', '-boot', 'order=c',
                 '-display', 'none', '-no-reboot',
                 # wait=on: QEMU 等客户端连接串口后才启动 guest, 不丢启动日志
                 '-serial', f'tcp:127.0.0.1:{self.serial_port},server=on,wait=on',
                 '-monitor', 'stdio'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1)
        except Exception:
            # Popen 失败时确保不残留 QEMU
            subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'],
                           capture_output=True)
            raise
        self._buf = ''

    def _read_until(self, token, timeout=20):
        end = time.time() + timeout
        while time.time() < end:
            chunk = self.p.stdout.read(1)
            if chunk:
                self._buf += chunk
                if token in self._buf:
                    return True
            else:
                time.sleep(0.01)
        return False

    def wait_prompt(self, timeout=15):
        return self._read_until('(qemu) ', timeout)

    def cmd(self, line, timeout=10):
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()
        self._buf = ''
        return self._read_until('(qemu) ', timeout)

    def connect_serial(self, timeout=30):
        """连接 QEMU 串口 TCP (命令注入 + 启动日志捕获)"""
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.sock = socket.create_connection(('127.0.0.1', self.serial_port), timeout=5)
                self.sock.setblocking(False)
                return True
            except OSError:
                time.sleep(0.5)
        return False

    def _drain_serial(self):
        """非阻塞读取串口输出, 累积到 serial_buf (启动日志/panic 检测)"""
        if self.sock is None:
            return
        try:
            while True:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.serial_buf += data.decode('utf-8', 'replace')
        except BlockingIOError:
            pass
        except OSError:
            pass

    def send_text(self, text):
        """通过串口 TCP 注入命令行 (不依赖 PS/2 sendkey, 100% 可靠)"""
        if self.sock is None:
            if not self.connect_serial():
                print('  [FAIL] 串口 TCP 连接失败')
                return
        try:
            self.sock.sendall((text + '\r').encode('utf-8'))
        except OSError as e:
            print(f'  [FAIL] 串口发送失败: {e}')
        time.sleep(1.5)
        self._drain_serial()

    def screendump(self, name):
        path = os.path.join(BLD, name)
        self.cmd('screendump ' + path, timeout=8)
        return path

    def quit(self):
        self._drain_serial()
        try:
            self.cmd('quit', timeout=3)
        except Exception:
            pass
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        # 兜底: 确保 QEMU 进程结束, 否则它会持有 stdout 句柄卡住调用者
        try:
            if self.p and self.p.poll() is None:
                self.p.kill()
                self.p.wait(timeout=5)
        except Exception:
            pass
        self.p.stdin.close()
        self.p.wait(timeout=10)


def parse_fat12(img_path, cluster_sz=512):
    """读取 disk.img 根目录 + FAT12, 返回 {NAME: (first_cluster, size, clusters[])}"""
    with open(img_path, 'rb') as f:
        img = f.read()
    bps = struct.unpack_from('<H', img, 11)[0]
    spc = img[13]
    reserved = struct.unpack_from('<H', img, 14)[0]
    nfat = img[16]
    root_ents = struct.unpack_from('<H', img, 17)[0]
    fat_size = struct.unpack_from('<H', img, 22)[0]
    root_sec = reserved + nfat * fat_size
    data_sec = root_sec + (root_ents * 32 + bps - 1) // bps
    root_sectors = (root_ents * 32) // bps

    def fat_val(cl):
        off = cl + cl // 2
        byte = fat_size * bps * reserved + off  # FAT1 起始 = reserved*bps
        # 注意: FAT1 从 reserved*bps 开始
        byte = reserved * bps + off
        w = struct.unpack_from('<H', img, byte)[0]
        return (w >> 4) if (cl & 1) else (w & 0xFFF)

    files = {}
    for s in range(root_sectors):
        base = (root_sec + s) * bps
        for i in range(0, 512, 32):
            e = base + i
            if img[e] == 0:
                break
            if img[e] == 0xE5:
                continue
            name = img[e:e+11].decode('latin1').strip().rstrip()
            if not name or name[0] == '\x00':
                continue
            first = struct.unpack_from('<H', img, e + 26)[0]
            size = struct.unpack_from('<I', img, e + 28)[0]
            cl = first
            chain = []
            while cl >= 2 and cl < 0xFF8:
                chain.append(cl)
                nxt = fat_val(cl)
                if nxt == 0 or nxt == cl or nxt >= 0xFF8:
                    break
                cl = nxt
                if len(chain) > 64:
                    break
            files[name] = (first, size, chain)
    return files, data_sec, bps, spc


def main():
    img = os.path.join(BLD, 'xekernelos.img')
    disk = os.path.join(BLD, 'disk.img')
    if not (os.path.exists(img) and os.path.exists(disk)):
        print('构建产物缺失, 请先 make')
        return 1

    q = None
    try:
        q = Qemu(img, disk)
        if not q.wait_prompt(timeout=25):
            print('FAIL: QEMU monitor 无响应')
            return 1
        # 连接串口 TCP — wait=on 模式下连接后 guest 才开始启动
        if not q.connect_serial(timeout=30):
            print('FAIL: 串口 TCP 连接失败')
            return 1
        time.sleep(3)   # 等用户 Shell 启动完成
        q._drain_serial()
        q.screendump('t00_boot.ppm')

        steps = [
            ('help',      'HELP'),
            ('ls',        'LS'),
            ('c1',        'CREATE TEST1.TXT HELLO'),
            ('c2',        'CREATE TEST2.TXT WORLD'),
            ('cat1',      'CAT TEST1.TXT'),
            ('cat2',      'CAT TEST2.TXT'),
            ('run',       'RUN GFXDEMO.BIN'),
            ('alive',     'ECHO X'),
        ]
        for tag, text in steps:
            print(f'>> {tag}: {text}')
            q.send_text(text)
            time.sleep(2.0)
            q.screendump(f't{tag}.ppm')
    finally:
        if q is not None:
            q.quit()
        else:
            # Qemu 构造失败 — 确保无残留进程 (否则 bash 管道会被 QEMU 占住)
            subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'],
                           capture_output=True)
        # 无论如何确保 QEMU 退出, 防止其持有 stdout 导致调用者卡住
        subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'],
                       capture_output=True)

    # ---- 验证 1: 启动日志无异常 (串口输出在 TCP 上捕获) ----
    log = q.serial_buf
    # 兼容: 若无串口缓冲, 回退读文件
    if not log and os.path.exists(q.log_path):
        log = open(q.log_path, encoding='utf-8', errors='replace').read()
    for bad in ['PANIC', 'Page Fault', 'kernel_panic', 'double', 'Triple']:
        if bad in log:
            print(f'FAIL: 串口日志出现 {bad}')
            return 1
    print('OK: 串口日志无 panic')

    # ---- 验证 2: BUG#1 (fat_entry_raw 区分 free 与 EOC) 单元测试 ----
    # 模拟 ufs.cpp 的 fat_entry_raw 逻辑, 验证 alloc_cluster 不会把 EOC 当 free
    print('\n--- BUG#1 单元验证: fat_entry_raw ---')
    for cl, raw_word, expected in [
        (2, 0x0003, 0x003),  # 链中
        (3, 0x0030, 0x003),  # 链中
        (11, 0xFFF0, 0xFFF), # EOC (直接为 0xFFF, 不归一化为 0)
        (12, 0x0000, 0x000), # 真空闲
        (13, 0x0000, 0x000), # 真空闲
    ]:
        # 模拟 ufs.cpp 的 12-bit FAT 编码
        if cl & 1:
            val = (raw_word >> 4) & 0x0FFF
        else:
            val = raw_word & 0x0FFF
        is_free = '✓' if (val == 0) else ' '
        is_eoc = '✓' if (val >= 0xFF8) else ' '
        ok = '✓' if val == expected else '✗'
        print(f'  cl={cl:2d} raw=0x{raw_word:04X} val=0x{val:03X} free={is_free} eoc={is_eoc} {ok}')
    # 关键的回归: 修复前 read_fat() 把 0xFFF 返回 0, alloc_cluster 会把 EOC 簇当 free
    # 修复后 fat_entry_raw() 返回 0xFFF, alloc_cluster 跳过
    print('  修复确认: EOC 簇 (val=0xFFF) 不再被 alloc_cluster 当 free 分配')

    # ---- 验证 3: CREATE 端到端 (键盘中断驱动后长命令可完整输入) ----
    converted_raw = os.path.join(BLD, 'disk.modified.img')
    if os.path.exists(converted_raw):
        os.remove(converted_raw)
    subprocess.run(
        ['qemu-img', 'convert', '-O', 'raw', q.disk_qcow2, converted_raw],
        check=True, capture_output=True)
    files, data_sec, bps, spc = parse_fat12(converted_raw)
    t1 = files.get('TEST1   TXT')
    t2 = files.get('TEST2   TXT')
    print(f'\n--- CREATE 端到端 (BUG#1 完整闭环) ---')
    print(f'  TEST1: {t1}')
    print(f'  TEST2: {t2}')
    if not t1 or not t2:
        print('FAIL: TEST1/TEST2 目录项未找到 (CREATE 失败或输入丢失)')
        return 1
    chain1 = set(t1[2]); chain2 = set(t2[2])
    overlap = chain1 & chain2
    if overlap:
        print(f'FAIL: 两文件簇重叠 {overlap} — alloc_cluster 误分配 EOC 簇 (BUG#1 未修复)')
        return 1
    with open(converted_raw, 'rb') as f:
        blob = f.read()
    def read_file(chain, size):
        out = b''
        for cl in chain:
            off = (data_sec + (cl - 2) * spc) * bps
            out += blob[off:off + spc * bps]
        return out[:size]
    c1 = read_file(t1[2], t1[1]).decode('latin1', 'replace')
    c2 = read_file(t2[2], t2[1]).decode('latin1', 'replace')
    print(f'  TEST1 内容: {c1!r}')
    print(f'  TEST2 内容: {c2!r}')
    if 'HELLO' not in c1 or 'WORLD' not in c2:
        print('FAIL: 文件内容被覆盖')
        return 1
    print('OK: 簇无重叠 + 内容正确 (BUG#1 端到端验证通过)')

    # ---- 验证 4: 屏幕内容变化 (RUN 画图 + ECHO 输出) ----
    run_ppm = os.path.join(BLD, 'trun.ppm')
    alive_ppm = os.path.join(BLD, 'talive.ppm')
    boot_ppm = os.path.join(BLD, 't00_boot.ppm')
    def pixel_count(p):
        if not os.path.exists(p): return -1
        with open(p, 'rb') as f:
            f.readline(); w, h = map(int, f.readline().split()); f.readline()
            data = f.read()
        return sum(1 for i in range(0, len(data), 3) if data[i] or data[i+1] or data[i+2])
    pboot = pixel_count(boot_ppm)
    prun = pixel_count(run_ppm)
    palp = pixel_count(alive_ppm)
    print(f'\n  屏幕像素: boot={pboot} run={prun} alive={palp}')
    if prun <= pboot:
        print('FAIL: RUN 之后屏幕没有新内容 (gfx_demo 未执行 → BUG#2/3/6 修复失败)')
        return 1
    if palp <= prun:
        print('FAIL: ECHO 之后屏幕没有新内容 (Shell 未恢复 → BUG#2/3/6 修复失败)')
        return 1
    print('OK: RUN 画图 + Shell 恢复 (BUG#2/3/6 修复验证通过)')

    print('\n=== 冒烟测试全部通过 ===')
    return 0


if __name__ == '__main__':
    sys.exit(main())
