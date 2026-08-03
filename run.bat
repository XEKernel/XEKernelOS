@echo off
cd /d %~dp0
echo XEKernelOS - Building...
call make clean
call make
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)
echo XEKernelOS - Starting QEMU...
qemu-system-i386 -hda build\xekernelos.img -hdb build\disk.img -m 32 -boot order=c -serial stdio
if errorlevel 1 (
    echo.
    echo QEMU failed. Make sure qemu-system-i386 is in PATH.
    pause
)
