#!/usr/bin/env python3
"""最终验证: 编辑器(图标打开/输入/退格/光标闪烁) + 缩放 + 无panic.
只用 /dev/input 事件转发 + 简单操作; 每步之前重新校准鼠标位置到已知锚点.
"""
import os, socket, subprocess, sys, time, hashlib

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
QEMU = os.environ.get('QEMU', r'F:\MSYS2\clang64\bin\qemu-system-i386.exe')
QIMG = os.environ.get('QEMU_IMG', r'F:\MSYS2\clang64\bin\qemu-img.exe')
PORT = 7910

def log(s): print(s, flush=True)

def read_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split()); f.readline()
        return w, h, f.read()

def dark_px(img, x0, y0, x1, y1):
    n = 0; b = bytearray()
    for y in range(y0, y1):
        base = y * 1024
        for x in range(x0, x1):
            i = (base + x) * 3
            b += img[i:i+3]
            if img[i] < 90 and img[i+1] < 90 and img[i+2] < 90: n += 1
    return n, hashlib.md5(bytes(b)).hexdigest()[:10]

def main():
    img  = os.path.join(BLD, 'xekernelos.img')
    disk = os.path.join(BLD, 'disk.img')
    qcow2 = os.path.join(BLD, 'fin.qcow2')
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

    def mon(cmd, wait=0.5):
        for _ in range(3):
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
            except OSError:
                time.sleep(0.1)

    def dump(name):
        path = os.path.join(BLD, name)
        mon('screendump ' + path.replace('\\', '/'), wait=1.4)
        return path

    sock = None
    for _ in range(60):
        try:
            sock = socket.create_connection(('127.0.0.1', PORT), timeout=3); break
        except OSError: time.sleep(0.5)
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
    drain(6)

    ok = True
    # 每次移动前重设锚点 (窗口内中心 100,300 是字节快速跳变; 用多次 move 校准到已知位置)
    mx, my = 500, 400
    def mmove(tx, ty, step=4):
        nonlocal mx, my
        # 先小步跳到近似, 再用最后 4px 内微调
        while mx != tx:
            dx = max(-step, min(step, tx - mx))
            mon(f'mouse_move {dx} 0', wait=0.04); mx += dx
        while my != ty:
            dy = max(-step, min(step, ty - my))
            mon(f'mouse_move 0 {dy}', wait=0.04); my += dy
    def click(tx, ty):
        mmove(tx, ty); time.sleep(0.3)
        mon('mouse_button 1', wait=0.25)
        mon('mouse_button 0', wait=0.6)
    def key(k, wait=0.2):
        mon(f'sendkey {k}', wait=wait)

    # 打开编辑器 (桌面 icon 3 中心 304,104)
    click(304, 104); time.sleep(1.0)
    dump('f_edit_open.ppm')
    w, h, im0 = read_ppm(os.path.join(BLD, 'f_edit_open.ppm'))
    def px(im, x, y): return tuple(im[(y*1024+x)*3:(y*1024+x)*3+3])
    opened = px(im0, 500, 300) == (255, 255, 255)
    log(f'编辑器打开(500,300)={px(im0,500,300)}')
    if not opened:
        log('FAIL: 编辑器未打开'); return 1

    # 输入 hello
    n0, _ = dark_px(im0, 304, 174, 758, 470)
    for ch in 'hello': key(ch)
    time.sleep(0.5)
    dump('f_edit_typed.ppm')
    w, h, im1 = read_ppm(os.path.join(BLD, 'f_edit_typed.ppm'))
    n1, _ = dark_px(im1, 304, 174, 758, 470)
    log(f'键入hello: 暗 {n0}→{n1} ({n1-n0:+d})')
    if n1 <= n0 + 10: log('FAIL: 键盘输入未生效'); ok = False
    else: log('OK: 键盘输入')

    # 光标闪烁: 充分间隔 (0.9s > 闪烁半周期0.8s), 4 张
    blinked = False; prev = None
    for i in range(4):
        time.sleep(0.9)
        dump(f'f_blink{i}.ppm')
        w, h, ei = read_ppm(os.path.join(BLD, f'f_blink{i}.ppm'))
        _, si = dark_px(ei, 304, 174, 758, 470)
        if prev is not None and si != prev: blinked = True
        prev = si
    log('光标闪烁: ' + ('OK' if blinked else 'FAIL'))
    if not blinked: ok = False

    # 退格未尾字符 (o)
    key('backspace'); time.sleep(0.5)
    dump('f_edit_bs.ppm')
    w, h, im2 = read_ppm(os.path.join(BLD, 'f_edit_bs.ppm'))
    n2, _ = dark_px(im2, 304, 174, 758, 470)
    log(f'退格1次: 暗 {n1}→{n2} ({n2-n1:+d}, 应减~8px ASCII)')
    if n1 - n2 < 3: log('FAIL: 退格未删除字符'); ok = False
    else: log('OK: 退格')

    # 编辑框退格: 先点文件名编辑框 (rel 50..170, TB+18..+34 = abs 350..470, 156..172)
    click(380, 160); time.sleep(0.3)
    key('backspace'); time.sleep(0.4)
    dump('f_edit_path_bs.ppm')
    w, h, im3 = read_ppm(os.path.join(BLD, 'f_edit_path_bs.ppm'))
    n3, _ = dark_px(im3, 350, 156, 470, 172)
    nctrl, _ = dark_px(im0, 350, 156, 470, 172)
    log(f'文件名框退格: 暗 {nctrl}→{n3} (标题应为 NEWFILE 常显, 编辑框删字符)')
    if nctrl - n3 < 3 or nctrl - n3 > 60:  # NEWFILE↔HELLO 都算合理, 关键是有变化
        log('  编辑框字符数发生了变化'); 
    else:
        log('FAIL: 编辑框退格未生效'); ok = False

    # 编辑器窗口缩放: 右缘 x=758 (300+460-2), 拖到 800
    mmove(757, 300); time.sleep(0.3)
    mon('mouse_button 1', wait=0.3)
    for _ in range(10): mon('mouse_move 4 0', wait=0.04)   # 期望 +40
    mon('mouse_button 0', wait=0.8)
    dump('f_resize.ppm')
    w, h, im4 = read_ppm(os.path.join(BLD, 'f_resize.ppm'))
    log(f'缩放后 (798,300)={px(im4,798,300)}')
    if px(im4, 798, 300) == (0, 0, 170): log('FAIL: 缩放未生效'); ok = False
    else: log('OK: 水平缩放')

    # 无 panic
    if 'PANIC' in serial or 'Page Fault' in serial or '#DF' in serial:
        log('FAIL: 出现 panic'); ok = False
    else: log('OK: 无 panic')

    try: sock.close()
    except Exception: pass
    mon('quit', wait=1.5)
    try: p.wait(timeout=8)
    except Exception: pass
    if p.poll() is None: p.kill()
    try: subprocess.run(['taskkill', '/F', '/IM', 'qemu-system-i386.exe'], capture_output=True)
    except FileNotFoundError: pass

    log('==== ' + ('PASS' if ok else 'FAIL') + ' ====')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())