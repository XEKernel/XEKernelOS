#!/usr/bin/env python3
"""超简化键盘中断测试: QEMU 启动 → sendkey 'X' → 截屏 → 退出"""
import os, subprocess, time, sys

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
BLD  = os.path.join(ROOT, 'build')

QEMU = 'qemu-system-i386'
DISK = os.path.join(BLD, 'disk.qcow2')
if not os.path.exists(DISK):
    subprocess.run(['qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', '-S', '4M',
                    os.path.join(BLD, 'disk.img'), DISK], check=True)

mon_log = os.path.join(BLD, 'kbd_mon.log')
ser_log = os.path.join(BLD, 'kbd_serial.log')
# 文件可能被 QEMU 占用, 不删除直接用

# 用 -monitor unix/mode=server 不可 (Windows). 改用 -monitor file.
# QEMU 会在 log 末尾添加 (qemu) prompt. 我们用 mutate 间隔写入指令.
# 但 file mode 不能交互. 用 stdio 同时 pipe stdin/stdout.
# 关键: Windows 下 QEMU 子进程 readline 同步问题. 用 re-open monitor on file.

# 方案: -monitor stdio 但 disable QEMU 关闭 stdin, 让我们的 PIPE 一直开
p = subprocess.Popen(
    [QEMU,
     '-drive', f'file={DISK},format=qcow2,if=ide,index=1',
     '-drive', f'file={os.path.join(BLD, "xekernelos.img")},format=raw,if=ide,index=0',
     '-m', '32', '-boot', 'order=c', '-display', 'none', '-no-reboot',
     '-serial', f'file:{ser_log}',
     '-monitor', 'stdio'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    text=True, bufsize=1)

def sendline(c):
    """向 monitor 写一行, 吃 OSError if QEMU 关闭 stdin"""
    try:
        p.stdin.write(c + '\n')
        p.stdin.flush()
    except (OSError, BrokenPipeError):
        pass

def read_until(token, timeout=10):
    end = time.time() + timeout
    buf = ''
    while time.time() < end:
        try:
            ch = p.stdout.read(1)
        except Exception:
            break
        if ch:
            buf += ch
            if token in buf:
                return buf
        else:
            time.sleep(0.02)
    return buf

print('Wait boot...')
# 等待 monitor 就绪
if not read_until('(qemu)', 20):
    print('FAIL: monitor ready timeout ')
    p.kill(); sys.exit(1)

# 等 shell 启动
time.sleep(4)

print('screendump before')
sendline('screendump ' + os.path.join(BLD, 'kbd_before.ppm'))
read_until('(qemu)', 5)

print('sendkey x (single)')
sendline('sendkey x')
read_until('(qemu)', 5)
time.sleep(2)

print('screendump after x')
sendline('screendump ' + os.path.join(BLD, 'kbd_after_x.ppm'))
read_until('(qemu)', 5)

print('quit')
sendline('quit')
try: p.stdin.close()
except: pass
try: p.wait(timeout=8)
except: p.kill()

print('=== serial log (after x) ===')
log = open(ser_log, encoding='utf-8', errors='replace').read()
# 找 'K' (我们的 debug 字符)
print('K 字符数:', log.count('K'))
print('末 200 字符:', log[-200:])

print('=== 像素对比 ===')
for n in ['before','after_x']:
    p2 = os.path.join(BLD, f'kbd_{n}.ppm')
    with open(p2,'rb') as f:
        f.readline(); w,h = map(int, f.readline().split()); f.readline()
        data = f.read()
    nb = sum(1 for i in range(0,len(data),3) if data[i] or data[i+1] or data[i+2])
    print(f'  kbd_{n}: 非黑像素 {nb}')
