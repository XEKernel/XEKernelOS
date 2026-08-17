#!/usr/bin/env python3
"""GUI 自动化测试: 资源管理器 新建文件夹(方块名?) / 删除文件夹 / 窗口缩放.
用 QEMU HMP mouse_move/mouse_button 驱动 PS/2 鼠标 (步长<=4 避免加速).
"""
import os, socket, subprocess, sys, time, struct

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
QEMU = os.environ.get('QEMU', r'F:\MSYS2\clang64\bin\qemu-system-i386.exe')
QIMG = os.environ.get('QEMU_IMG', r'F:\MSYS2\clang64\bin\qemu-img.exe')
PORT = 7810

def log(s): print(s, flush=True)

def prep_disk():
    """复制 disk.img → 去掉 NEWDIR 条目 (0xE5 + FAT 释放), 供干净测试."""
    src = os.path.join(BLD, 'disk.img')
    raw = bytearray(open(src, 'rb').read())
    root = 19 * 512
    removed = 0
    for i in range(0, 14 * 512, 32):
        e = root + i
        if raw[e] == 0: break
        if raw[e:e+6] == b'NEWDIR' and raw[e+11] & 0x10:
            cl = struct.unpack_from('<H', raw, e + 26)[0]
            for fat_base in (1 * 512, (1 + 9) * 512):  # 两份 FAT
                off = fat_base + cl + cl // 2
                if cl & 1:
                    raw[off] &= 0x0F
                else:
                    raw[off] = 0
                    raw[off + 1] &= 0xF0
            raw[e] = 0xE5
            removed += 1
    clean = os.path.join(BLD, 'gui_base.img')
    open(clean, 'wb').write(raw)
    log(f'基础镜像: 移除 {removed} 个 NEWDIR')
    return clean

def read_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split()); f.readline()
        return w, h, f.read()

def dark(img, x, y, w):
    """(x,y) 起 w 像素行中暗像素数"""
    n = 0
    for i in range(w):
        j = (y * 1024 + x + i) * 3
        if img[j] < 100 and img[j+1] < 100 and img[j+2] < 100: n += 1
    return n

def count_boxes(img, x0, x1, y):
    """在 [x0,x1) 扫描 16px 宽窗口: 顶行+底行都 >=13/16 暗 → 空心方块字形"""
    boxes = 0
    x = x0
    while x + 16 <= x1:
        if dark(img, x, y, 16) >= 13 and dark(img, x, y + 15, 16) >= 13:
            boxes += 1
            x += 16   # 方块不重叠
        else:
            x += 2
    return boxes

def main():
    base_img = prep_disk()
    img  = os.path.join(BLD, 'xekernelos.img')
    qcow2 = os.path.join(BLD, 'gui_test.qcow2')
    if os.path.exists(qcow2): os.remove(qcow2)
    subprocess.run([QIMG, 'convert', '-f', 'raw', '-O', 'qcow2', '-S', '4M',
                    base_img, qcow2], check=True, capture_output=True)
    p = subprocess.Popen([QEMU,
        '-drive', f'file={qcow2},format=qcow2,if=ide,index=1',
        '-drive', f'file={img},format=raw,if=ide,index=0',
        '-m', '32', '-boot', 'order=c', '-display', 'none', '-no-reboot',
        '-serial', f'tcp:127.0.0.1:{PORT},server=on,wait=on',
        '-monitor', f'tcp:127.0.0.1:{PORT+1},server=on,wait=off'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    mon_buf = []
    def mon(cmd, wait=0.4):
        try:
            m = socket.create_connection(('127.0.0.1', PORT + 1), timeout=3)
            m.settimeout(0.2)
            try:
                while True:
                    d = m.recv(4096)
                    if not d: break
            except OSError: pass
            m.sendall((cmd + '\n').encode()); time.sleep(wait)
            try:
                while True:
                    d = m.recv(4096)
                    if not d: break
                    mon_buf.append(d.decode('utf-8', 'replace'))
            except OSError: pass
            m.close()
        except OSError as e:
            log(f'mon err {cmd}: {e}')

    def screendump(name):
        path = os.path.join(BLD, name)
        mon('screendump ' + path.replace('\\', '/'), wait=1.2)
        return path

    sock = None
    for _ in range(60):
        try:
            sock = socket.create_connection(('127.0.0.1', PORT), timeout=3); break
        except OSError: time.sleep(0.5)
    if not sock: log('FAIL: serial'); return 1
    sock.settimeout(0.2)
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
    drain(5)   # GUI 起来

    # ---- 鼠标: 初始 (512,384), 步长 4 无加速 ----
    mx, my = 512, 384
    def mmove(tx, ty, step=0.035):
        nonlocal mx, my
        while mx != tx or my != ty:
            dx = max(-4, min(4, tx - mx)); dy = max(-4, min(4, ty - my))
            mon(f'mouse_move {dx} {dy}', wait=step)
            mx += dx; my += dy
    def click(tx, ty):
        mmove(tx, ty)
        time.sleep(0.3)
        mon('mouse_button 1', wait=0.25)
        mon('mouse_button 0', wait=0.5)

    # 1. 打开资源管理器 (文件 icon 0: x=60..108, y=80..128, 中心 84,104)
    click(84, 104)
    time.sleep(1.0)
    dumpA = screendump('gt_expl.ppm')

    # 2. 点 "新建文件夹" 按钮 (explorer 60,150 400x300; 按钮1 abs x 314..382, y 170..186)
    click(348, 178)
    time.sleep(1.0)
    dumpB = screendump('gt_mkdir.ppm')

    ok = True
    w, h, imgA = read_ppm(dumpA)
    w, h, imgB = read_ppm(dumpB)

    # 控制组: 第一行文件 README.TXT (y=206, ASCII 正常渲染)
    ctrl_boxes = count_boxes(imgA, 62, 250, 206)
    # NEWDIR 行: 12 文件后 → y = 206 + 12*16 = 398
    rowB_boxes = count_boxes(imgB, 84, 200, 398)
    rowB_dark  = dark(imgB, 62, 398, 16*10)
    rowA_dark  = dark(imgA, 62, 398, 16*10)
    log(f'控制行(README.TXT y=206) 方块数={ctrl_boxes} (期望 0)')
    log(f'新建后 NEWDIR 行(y=398): 方块数={rowB_boxes} 暗px={rowB_dark} (建前 {rowA_dark})')
    if ctrl_boxes > 0: log('异常: 正常文件名也渲染成方块?!'); ok = False
    if rowA_dark > 5: log('异常: 建前该行已有内容 (行号计算错)'); ok = False
    if rowB_dark < 5:
        log('FAIL: NEWDIR 行无任何内容 — 目录未显示'); ok = False
    elif rowB_boxes >= 2:
        log(f'确认复现: 文件夹名渲染为 {rowB_boxes} 个方块 (而非 ASCII)'); 

    # 3. 选中 NEWDIR 行 (y=398, 点名称区) → 高亮; 再点 删除 按钮 (x 386..454)
    click(110, 406)
    time.sleep(0.8)
    dumpC = screendump('gt_sel.ppm')
    click(420, 178)
    time.sleep(1.0)
    dumpD = screendump('gt_rmdir.ppm')
    w, h, imgD = read_ppm(dumpD)
    rowD_dark = dark(imgD, 62, 398, 160)
    log(f'删除后 NEWDIR 行(y=398): 暗px={rowD_dark} (期望≈0=已删, 建={rowB_dark})')
    if rowD_dark > 20:
        log('FAIL: 删除后行仍在 — RMDIR 未生效'); ok = False
    else:
        log('OK: 删除文件夹生效 (屏幕行消失)')

    # 4. 窗口缩放测试: 拖右边缘 (x=458, y=300) → 右移 60px; 高度不变
    #    explorer 窗口 (60,150,400,300): 右边缘 abs x∈[454,460)
    mmove(457, 300); time.sleep(0.3)
    mon('mouse_button 1', wait=0.3)
    for _ in range(15): mon('mouse_move 4 0', wait=0.03)   # +60px 宽
    mon('mouse_button 0', wait=0.8)
    time.sleep(0.8)
    dumpE = screendump('gt_resize.ppm')
    # 窗口右边缘 x = 60 + 460 = 520: 检查 (520±2, 300) 处白色边框像素
    def px(im, x, y):
        j = (y * 1024 + x) * 3
        return tuple(im[j:j+3])
    edge = px(imgD, 459, 300)
    after_wide = px(read_ppm(dumpE)[2], 519, 300)
    log(f'缩放前右边缘(459,300)={edge}  缩放后(519,300)={after_wide}')
    log('--- GUI 调试串口 ---')
    open(os.path.join(BLD, 'gui_serial.log'), 'w').write(serial)
    for l in serial.splitlines():
        if 'sched' not in l and l.strip():
            log('  ' + l.strip()[:120])

    try: sock.close()
    except Exception: pass
    mon('quit', wait=1.5)
    try: p.wait(timeout=8)
    except Exception: pass
    if p.poll() is None: p.kill()
    try: subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'], capture_output=True)
    except FileNotFoundError: pass

    # 磁盘终态
    rawf = os.path.join(BLD, 'gui_test.raw')
    if os.path.exists(rawf): os.remove(rawf)
    subprocess.run([QIMG, 'convert', '-O', 'raw', qcow2, rawf], check=True, capture_output=True)
    data = open(rawf, 'rb').read()
    root = 19 * 512
    log('--- 磁盘根目录终态 ---')
    for i in range(0, 14 * 512, 32):
        e = root + i
        if data[e] == 0: break
        if data[e] == 0xE5:
            log(f'  [已删] {data[e+1:e+11]!r}')
            continue
        log(f'  {data[e:e+11]!r} attr=0x{data[e+11]:02X} clus={struct.unpack_from("<H", data, e+26)[0]}')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
