# XEKernelOS

一个从零手写的 **x86 32 位保护模式操作系统内核**，严格遵循自定开发宪章 5 条准则。

---

## 特性

| 分类 | 功能 |
|------|------|
| **启动** | MBR → Stage2 三段启动，实模式→保护模式切换 |
| **图形** | VBE 1024×768×32bpp 线性帧缓冲，8×16 ASCII + 16×16 中文字体(27826字) |
| **中断** | PIC 重映射(0x20/0x28)，48 条目 IDT + ISR 分发器 |
| **定时器** | PIT 100Hz |
| **输入** | PS/2 键盘轮询，PS/2 鼠标驱动(灵敏度 1x+1.5x加速) |
| **内存** | 位图物理页分配器(4KB)，PSE 4MB 大页分页 |
| **磁盘** | ATA PIO 扇区读写(28-bit LBA) |
| **文件系统** | FAT12 双驱动：内核(bcache) + 用户态(ufs.cpp裸扇区) |
| **用户态** | Ring 3 + TSS，抢占式 O(1) 动态优先级调度 |
| **Shell** | 内核 Shell(19命令) + 用户 Shell(14命令)，双色中文界面 |

---

## 开发宪章 (5条准则)

| # | 准则 | 实现 |
|---|------|------|
| 1 | **接口统一** — 万物皆 fd | GFX→fd0 write/ioctl |
| 2 | **对象状态机** — fd 对应 C++ 类 | file/pipe/fb 四类型 |
| 3 | **响应优先** — 动态优先级 | O(1)调度+IRQ提升+衰减 |
| 4 | **令牌权限** — uint32_t caps | 继承+只减不增 |
| 5 | **内核边界** — FS 在用户态 | ufs.cpp完整FAT12读写 |

---

## 项目结构

```
src/
├── boot/              引导层 (NASM)
├── kernel/             内核核心 (C++)
│   ├── kernel.cpp, isr.cpp, idt.cpp, mm.cpp
│   ├── paging.cpp, task.cpp, syscall.cpp
│   ├── loader.cpp, user.cpp, panic.cpp
├── drivers/            硬件驱动 (C++)
│   ├── gfx.cpp, keyboard.cpp, mouse.cpp
│   ├── ata.cpp, bcache.cpp, pic.cpp, pit.cpp
│   ├── serial.cpp, font8x16.h, font_cn.h, font_cn_load.cpp
├── fs/                 文件系统
│   ├── fat12.cpp       内核 FAT12 (bcache)
│   └── ramdisk.cpp     内存文件系统
├── shell/              Shell
│   └── shell.cpp       内核 Shell (Ring 0)
├── user/               用户态 (Ring 3)
│   ├── ushell.cpp      用户 Shell
│   ├── ufs.cpp         用户态 FAT12 完整读写驱动
│   ├── usys.h          用户态 syscall 包装
│   └── user.ld         ��户态链接脚本
├── lib/                公共库
│   ├── types.h, ports.h, heap.cpp
│   ├── list.h, strutil.cpp, cpprt.cpp
└── linker.ld           内核链接脚本
```

---

## 构建运行

```bash
# 依赖: NASM, Clang++(i686-elf), LLD, Python 3, QEMU
make          # 编译内核+用户Shell+��盘镜像
make run      # QEMU 启动
```

---

## Shell 命令

### 内核 Shell (`XEKernel\>`)
```
HELP     INFO     MEM      CLEAR    LS       CAT
CD       MKDIR    RMDIR    CP       MV       RM
CREATE   MOUSE    RUN      ECHO     REBOOT   SHUTDOWN
DISK ID  DISK R   DISK W   USERSH
```

### 用户 Shell (`XEKernel@Xek\路径>`)
```
HELP     CLEAR    LS       CD       ECHO     MKDIR
RMDIR    RM       MV       CREATE   TIME     TMP
RUN      EXIT
```

LS 输出格式: `类型 DIR/空格 大小(9位右齐) 日期(YYYY-MM-DD HH:MM) 文件名`

---

## License

MIT
