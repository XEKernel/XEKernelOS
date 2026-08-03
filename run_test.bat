@echo off
rem XEKernelOS 手动测试入口 — 窗口模式 + 串口日志
rem 用法: run_test.bat [port]
rem   不填 port: 串口走文件 build\serial.log (调试信息落盘)
rem   填 port  : 串口走 TCP (可用 qemu_auto.py / MCP 注入命令)
setlocal
cd /d %~dp0

if not exist build\xekernelos.img (
    echo [ERROR] 未找到 build\xekernelos.img, 请先构建: make
    pause
    exit /b 1
)

if "%~1"=="" (
    echo [INFO] 串口日志 -> build\serial.log
    qemu-system-i386 ^
        -drive file=build\xekernelos.img,format=raw,if=ide,index=0 ^
        -drive file=build\disk.img,format=raw,if=ide,index=1 ^
        -m 32 -boot order=c -no-reboot ^
        -serial file:build\serial.log
) else (
    echo [INFO] 串口 -> tcp:127.0.0.1:%~1 (server, wait=on)
    qemu-system-i386 ^
        -drive file=build\xekernelos.img,format=raw,if=ide,index=0 ^
        -drive file=build\disk.img,format=raw,if=ide,index=1 ^
        -m 32 -boot order=c -no-reboot ^
        -serial tcp:127.0.0.1:%~1,server=on,wait=on
)

if errorlevel 1 (
    echo.
    echo [ERROR] QEMU 启动失败. 确认 qemu-system-i386 在 PATH 中.
    pause
)
