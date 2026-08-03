#!/usr/bin/env bash
# ============================================================
# XEKernelOS 构建环境 (Windows + MSYS2 clang64)
#
# 用法 (Git Bash):
#   source tools/env.sh && make
#   source tools/env.sh && python tools/qemu_auto.py
#   source tools/env.sh && qemu-system-i386 ...
# ============================================================

# MSYS2 clang64 工具链 (clang / lld / llvm-objcopy / nasm / qemu / qemu-img)
MSYS2_CLANG64="/f/MSYS2/clang64/bin"
if [ -d "$MSYS2_CLANG64" ]; then
    export PATH="$MSYS2_CLANG64:$PATH"
fi

# make: MSYS2 的 mingw32-make 是 mingw 环境下的 make
if ! command -v make >/dev/null 2>&1 && command -v mingw32-make >/dev/null 2>&1; then
    make() { mingw32-make "$@"; }
    export -f make 2>/dev/null || true
fi

echo "[env] clang:   $(clang++ --version 2>/dev/null | head -1)"
echo "[env] nasm:    $(nasm -v 2>/dev/null)"
echo "[env] qemu:    $(qemu-system-i386 --version 2>/dev/null | head -1)"
echo "[env] python:  $(python --version 2>/dev/null)"
