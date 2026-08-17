#!/usr/bin/env python3
"""综合验证: 重命名 / 删除 / 编辑器中文退格 / 光标闪烁 / 缩放."""
import os, socket, subprocess, sys, time, struct, hashlib

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
QEMU = os.environ.get('QEMU', r'F:\MSYS2\clang64\bin\qemu-system-i386.exe')
QIMG = os.environ.get('QEMU_IMG', r'F:\MSYS2\clang64\bin\qemu-img.exe')
PORT = 7850

def log(s): print(s, flush=True)

def read_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split()); f.readline()
        return w, h, f.read()

def dark_px(img, x0, y0, x1, y1):
    """区域内暗像素集合签名与数量"""
    n = 0; b = bytearray()
    for y in range(y0, y1):
        base = y * 1024
        for x in range(x0, x1):
            i = (base + x) * 3
            p = img[i:i+3]
            b += p
            if p[0] < 90 and p[1] < 90 and p[2] < 90: n += 1
    return n, hashlib.md5(bytes(b)).hexdigest()[:10]

def main():
    img  = os.path.join(BLD, 'xekernelos.img')
    disk = os.path.join(BLD, 'disk.img')
    qcow2 = os.path.join(BLD, 'gt2.qcow2')
    if os.path.exists(qcow2): os.remove(qcow2)
    subprocess.run([QIMG, 'convert', '-f', 'raw', '-O', 'qcow2', '-S', '4M',
                    disk, qcow2], check=True, capture_output=True)
    p = subprocess.Popen([QEMU,
        '-drive', f'file={qcow2},format=qcow2,if=ide,index=1',
        '-drive', f'file={img},format=raw,if=ide,index=0',
        '-m', '32', '-boot', 'order=c', '-display', 'none', '-no-reboot',
        '-serial', f'tcp:127.0.0.1:{PORT},server=on,wait=on',
        '-monitor', f'tcp:127.0.0.1:{PORT+1},server=on,wait=off'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    mon_errs = [0]
    def mon(cmd, wait=0.4):
        for attempt in range(3):
            try:
                m = socket.create_connection(('127.0.0.1', PORT + 1), timeout=3)
                m.settimeout(0.15)
                try:
                    while True:
                        if not m.recv(4096): break
                except OSError: pass
                m.sendall((cmd + '\n').encode()); time.sleep(wait)
                try:
                    while True:
                        if not m.recv(4096): break
                except OSError: pass
                m.close()
                return
            except OSError as e:
                mon_errs[0] += 1
                time.sleep(0.1)
        log(f'mon FAILED: {cmd}')

    def dump(name):
        path = os.path.join(BLD, name)
        mon('screendump ' + path.replace('\\', '/'), wait=1.3)
        return path

    sock = None
    for _ in range(60):
        try:
            sock = socket.create_connection(('127.0.0.1', PORT), timeout=3); break
        except OSError: time.sleep(0.5)
    if not sock: log('FAIL serial'); return 1
    sock.settimeout(0.1)
    serial = ''
    def drain(d):
        nonlocal serial
        end = time.time() + d
        while time.time() < end:
            try:
                x = sock.recv(4096)
                if not x: break
                serial += x.decode('utf-8', 'replace')
            except socket.timeout: pass
            except OSError: break
    end = time.time() + 25
    while time.time() < end and 'tasks ready' not in serial: drain(0.5)
    drain(5)

    mx, my = 512, 384
    def mmove(tx, ty):
        nonlocal mx, my
        while mx != tx or my != ty:
            dx = max(-4, min(4, tx - mx)); dy = max(-4, min(4, ty - my))
            mon(f'mouse_move {dx} {dy}', wait=0.03)
            mx += dx; my += dy
    def click(tx, ty):
        mmove(tx, ty); time.sleep(0.25)
        mon('mouse_button 1', wait=0.22)
        mon('mouse_button 0', wait=0.55)
    def key(k, wait=0.15):
        mon(f'sendkey {k}', wait=wait)

    ok = True
    # ---- 1. 探险器 + 新建文件夹 ----
    click(84, 104); time.sleep(0.8)                    # 文件 icon
    click(274, 178); time.sleep(0.8)                   # 新建文件夹 (abs 240..308)
    dump('g2_mkdir.ppm')
    click(110, 406); time.sleep(0.6)                   # 选中 NEWDIR (row12 y399)
    dump('g2_sel.ppm')
    w, h, im = read_ppm(os.path.join(BLD, 'g2_sel.ppm'))
    band = sum(1 for x in range(62, 272) if tuple(im[(402*1024+x)*3:(402*1024+x)*3+3]) == (0,0,170))
    log(f'选中行蓝色带宽(期望≈210): {band}')
    if band < 150: log('FAIL: 未选中'); ok = False

    # ---- 2. 重命名 NEWDIR → MYDIR ----
    click(348, 178); time.sleep(0.8)                   # 重命名按钮 (abs 314..382)
    click(410, 282); time.sleep(0.3)                   # 点编辑框聚焦 (rel 10..210, TB+36..+52)
    for _ in range(6): key('backspace')                # 清除 NEWDIR
    for ch in 'mydir': key(ch)
    dump('g2_renwin.ppm')
    click(420, 311); time.sleep(1.0)                   # 确定 (rel 90..150, TB+62..+84)
    dump('g2_renOk.ppm')

    # ---- 3. 选中 MYDIR 并删除 ----
    # 重命名后 esel 仍=12, 直接点行会触发"二次点击打开"(cd 进目录):
    # 先回上级(若已进入), 再点别的行清 esel, 再选 MYDIR
    click(110, 198); time.sleep(0.5)                   # [..] 返回上级
    click(110, 390); time.sleep(0.4)                   # row11 清 esel
    click(110, 406); time.sleep(0.4)                   # row12 选中 MYDIR
    click(420, 178); time.sleep(1.0)                   # 删除按钮 (abs 386..454)
    dump('g2_rmdir.ppm')

    # ---- 4. 编辑器: 桌面图标打开 (大目标, 避免双击漂移) ----
    click(294, 104); time.sleep(1.0)                   # 编辑器 icon (280..328, 80..128)
    dump('g2_edit0.ppm')
    w, h, ek0 = read_ppm(os.path.join(BLD, 'g2_edit0.ppm'))
    def ekpx(im, x, y): return tuple(im[(y*1024+x)*3:(y*1024+x)*3+3])
    opened = ekpx(ek0, 500, 300) == (255, 255, 255)
    log(f'编辑器窗口(500,300)={ekpx(ek0,500,300)} (白=已开)')
    if not opened:
        log('FAIL: 编辑器未打开 (点击漂移?)'); ok = False
    else:
        # 键入 abc → 文本出现
        n0, _ = dark_px(ek0, 304, 174, 750, 470)
        for ch in 'abc': key(ch)
        time.sleep(0.5)
        dump('g2_edit1.ppm')
        w, h, e1 = read_ppm(os.path.join(BLD, 'g2_edit1.ppm'))
        n1, s1 = dark_px(e1, 304, 174, 750, 470)
        log(f'键入abc后: 暗 {n0}→{n1} (应增加)')
        if n1 <= n0 + 10: log('FAIL: 编辑器键盘输入未生效'); ok = False
        else: log('OK: 编辑器键盘输入')
        # 光标闪烁: 3 张截图, 任一相邻对不同即通过
        blinked = False
        last_sig = s1
        for i in range(3):
            time.sleep(0.7)
            dump(f'g2_blk{i}.ppm')
            w, h, ei = read_ppm(os.path.join(BLD, f'g2_blk{i}.ppm'))
            ni, si = dark_px(ei, 304, 174, 750, 470)
            if si != last_sig: blinked = True
            last_sig = si
        log('光标闪烁: ' + ('OK' if blinked else 'FAIL (3张相同)'))
        if not blinked: ok = False
        # 退格 ×1 → 'c' 消失 (8px ASCII 字符)
        key('backspace'); time.sleep(0.5)
        dump('g2_edit3.ppm')
        w, h, e3 = read_ppm(os.path.join(BLD, 'g2_edit3.ppm'))
        n3, _ = dark_px(e3, 304, 174, 750, 470)
        log(f'退格后: 暗={n3} (从 {n1})')
        if n1 - n3 < 5: log('FAIL: 退格未删除字符'); ok = False
        else: log('OK: 退格删除完整字符')

    # ---- 5. 窗口右缘缩放 (编辑器在 300,120 460x360 → 右缘 x=760) ----
    mmove(757, 300); time.sleep(0.3)
    mon('mouse_button 1', wait=0.3)
    for _ in range(15): mon('mouse_move 4 0', wait=0.03)
    mon('mouse_button 0', wait=0.9)
    dump('g2_resize.ppm')
    w, h, rz = read_ppm(os.path.join(BLD, 'g2_resize.ppm'))
    def px(im, x, y): return tuple(im[(y*1024+x)*3:(y*1024+x)*3+3])
    log(f'缩放后 (817,300)={px(rz,817,300)} (白/灰=右缘移动成功, 蓝色=失败)')
    if px(rz, 817, 300) == (0, 0, 170): log('FAIL: 水平缩放未生效'); ok = False
    else: log('OK: 右缘水平缩放')

    try: sock.close()
    except Exception: pass
    mon('quit', wait=1.5)
    try: p.wait(timeout=8)
    except Exception: pass
    if p.poll() is None: p.kill()
    try: subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'], capture_output=True)
    except FileNotFoundError: pass

    # ---- 磁盘终态 ----
    rawf = os.path.join(BLD, 'gt2.raw')
    if os.path.exists(rawf): os.remove(rawf)
    subprocess.run([QIMG, 'convert', '-O', 'raw', qcow2, rawf], check=True, capture_output=True)
    data = open(rawf, 'rb').read()
    root = 19 * 512
    names = []
    log('--- 磁盘终态 ---')
    for i in range(0, 14 * 512, 32):
        e = root + i
        if data[e] == 0: break
        nm = data[e:e+11]; attr = data[e+11]
        if data[e] == 0xE5:
            log(f'  [已删] {data[e+1:e+11]!r}')
            continue
        log(f'  {nm!r} attr=0x{attr:02X}')
        names.append(nm)
    if any(n.startswith(b'MYDIR') for n in names):
        log('FAIL: MYDIR 仍在磁盘 (重命名后删除应移除)'); ok = False
    if any(n.startswith(b'NEWDIR') for n in names):
        log('FAIL: NEWDIR 仍在磁盘 (应已重命名)'); ok = False

    log(f'mon 重试次数: {mon_errs[0]}')
    log('==== 结果: ' + ('PASS' if ok else 'FAIL') + ' ====')
    open(os.path.join(BLD, 'gt2_serial.log'), 'w').write(serial)
    rates = [l.strip() for l in serial.splitlines() if 'rate' in l]
    log(f'idle rate 序列: {rates}')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
