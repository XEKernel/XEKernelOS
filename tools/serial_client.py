#!/usr/bin/env python3
"""XEKernelOS 串口注入客户端 — 连接 QEMU 串口 TCP, 发送命令 + 实时看调试输出.
用法:
  1. 先开 QEMU:  run_test.bat 7777
  2. 再开本客户端: python tools/serial_client.py 7777
  3. 输入命令回车即注入; 串口调试信息实时滚动显示.
  输入 quit 退出.
"""
import socket, sys, threading, time

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
    host = '127.0.0.1'
    print(f'[INFO] 连接 {host}:{port} ...')
    # 等待 QEMU 串口 server (wait=on 会等客户端连接后才启动 guest)
    for i in range(60):
        try:
            s = socket.create_connection((host, port), timeout=3)
            break
        except OSError:
            time.sleep(0.5)
    else:
        print('[FAIL] 60 秒内未连上串口. 确认 run_test.bat 7777 已启动.')
        return 1
    s.settimeout(0.2)
    print('[INFO] 已连接! 输入命令回车注入; quit 退出.\n')

    stop = threading.Event()

    def reader():
        buf = b''
        while not stop.is_set():
            try:
                data = s.recv(4096)
                if not data:
                    break
                buf += data
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    print(f'  [SER] {line.decode("utf-8", "replace")}')
            except socket.timeout:
                pass
            except OSError:
                break

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    try:
        while True:
            try:
                line = input('CMD> ')
            except (EOFError, KeyboardInterrupt):
                break
            if line.strip() in ('quit', 'exit', 'q'):
                break
            s.sendall((line + '\r').encode('utf-8'))
    finally:
        stop.set()
        s.close()
    print('[INFO] 已断开.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
