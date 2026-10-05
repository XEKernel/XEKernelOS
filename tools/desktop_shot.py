#!/usr/bin/env python3
"""截一张 QEMU 里的桌面截图并转成 PNG (供人工/多模态检查渲染完整性)。

用途: 排查"窗口标题栏文字/按钮、桌面图标文字、右键菜单没渲染出来"这类问题 ——
把屏幕抓下来直接看, 比在串口里数像素直观得多。

用法:
    source tools/env.sh && python tools/desktop_shot.py [输出名] [等待秒数]

默认: out = build/desktop_shot.png, 等待 4 秒 (内核启动 ~3s + 桌面绘制)

实现要点 (照抄 tools/gui_test.py 的成熟套路):
  · -display none 下 VGA 设备仍在渲染, monitor 的 screendump 可用
  · serial/monitor 走 TCP, 避免 Windows 下 stdio 管道的同步问题
  · screendump 出的是 P6 PPM (24bit), 这里纯 Python 转 PNG (不依赖 PIL)
"""
import os, socket, struct, subprocess, sys, time, zlib

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')
QEMU = os.environ.get('QEMU', 'qemu-system-i386')
PORT = 7950

out_name = sys.argv[1] if len(sys.argv) > 1 else 'desktop_shot.png'
wait_s   = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # P6\n<w> <h>\n255\n
    parts, idx = [], 3
    while len(parts) < 3:
        while data[idx:idx + 1].isspace():
            idx += 1
        s = idx
        while not data[idx:idx + 1].isspace():
            idx += 1
        parts.append(int(data[s:idx]))
    idx += 1
    w, h, _ = parts
    return w, h, data[idx:idx + w * h * 3]


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return (struct.pack('>I', len(d)) + t + d +
                struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    img  = os.path.join(BLD, 'xekernelos.img')
    disk = os.path.join(BLD, 'disk.img')
    ppm  = os.path.join(BLD, '_shot.ppm')
    png  = os.path.join(BLD, out_name)
    for f in (ppm, png):
        if os.path.exists(f):
            os.remove(f)

    p = subprocess.Popen([QEMU,
        '-drive', f'file={img},format=raw,if=ide,index=0',
        '-drive', f'file={disk},format=raw,if=ide,index=1',
        '-m', '256', '-boot', 'order=c', '-display', 'none', '-no-reboot',
        '-serial', f'tcp:127.0.0.1:{PORT},server=on,wait=on',
        '-monitor', f'tcp:127.0.0.1:{PORT + 1},server=on,wait=off'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ser = None
    for _ in range(60):
        try:
            ser = socket.create_connection(('127.0.0.1', PORT), timeout=3)
            break
        except OSError:
            time.sleep(0.5)
    if ser is None:
        print('FAIL: serial 连接超时'); p.kill(); return 1

    # 读到"进入用户态"之后就再等若干秒, 让桌面把首帧画完
    ser.settimeout(1.0)
    seen = b''
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            d = ser.recv(4096)
            if not d:
                break
            seen += d
        except OSError:
            pass
        if b'ring3 sys=' in seen or b'RING0 ' in seen or b'enter_user' in seen:
            break
    time.sleep(wait_s)

    # screendump (monitor)
    try:
        m = socket.create_connection(('127.0.0.1', PORT + 1), timeout=3)
        m.settimeout(0.3)
        try:
            while m.recv(4096):
                pass
        except OSError:
            pass
        m.sendall(('screendump ' + ppm.replace('\\', '/') + '\n').encode())
        time.sleep(2.0)
        m.close()
    except OSError as e:
        print('FAIL: monitor 错误', e); p.kill(); return 1

    p.kill()
    try:
        p.wait(timeout=5)
    except Exception:
        pass

    if not os.path.exists(ppm):
        print('FAIL: 没有生成 screendump'); return 1
    w, h, rgb = read_ppm(ppm)
    write_png(png, w, h, rgb)
    os.remove(ppm)

    # 顺便统计"墨水"分布: 每 64 行里非黑像素数与主要颜色, 便于无图时判断
    print(f'OK: {png}  {w}x{h}')
    for y0 in range(0, h, 64):
        ink = 0; colors = {}
        for y in range(y0, min(y0 + 64, h)):
            row = rgb[y * w * 3:(y + 1) * w * 3]
            for x in range(0, w, 2):
                r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
                if r or g or b:
                    ink += 1
                    colors[(r, g, b)] = colors.get((r, g, b), 0) + 1
        top = sorted(colors.items(), key=lambda kv: -kv[1])[:2]
        print(f'  y={y0:4d}-{min(y0+63,h):4d} ink={ink:6d}  {top}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
