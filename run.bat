@echo off
cd /d %~dp0
echo XEKernelOS - Starting QEMU...
qemu-system-i386 -fda build\xekernelos.img -hda build\disk.img -m 32 -boot order=a -serial stdio
if errorlevel 1 (
    echo.
    echo QEMU failed. Make sure qemu-system-i386 is in PATH.
    pause
)
