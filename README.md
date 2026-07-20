# XEKernelOS

一个从零手写的 **x86 32 位保护模式操作系统内核**，支持 VBE 图形界面、FAT12 文件系统和交互式 Shell。

---

## 特性

| 分类 | 功能 |
|------|------|
| **启动** | MBR → Stage2 三段启动，实模式→保护模式切换 |
| **图形** | VBE 1024×768×32bpp 线性帧缓冲，8×16 位图字体 |
| **中断** | PIC 重映射(0x20/0x28)，48 条目 IDT + ISR 分发器 |
| **定时器** | PIT 100Hz 可编程定时器 |
| **输入** | PS/2 键盘轮询 + 中断混合驱动，PS/2 鼠标驱动 |
| **内存** | 位图物理页分配器(4KB 页)，PSE 4MB 大页分页 |
| **磁盘** | ATA PIO 扇区读写(28-bit LBA) |
| **文件系统** | FAT12 读写(BPB 解析、目录、簇链、MKDIR/CAT/RM) |
| **用户态** | Ring 3 用户模式 + TSS，抢占式 Round-Robin 调度 |
| **Shell** | 17 个命令，彩色终端，UTF-8 中文支持 |

---

## 项目结构

```
src/
├── boot/              引导层 (NASM)
│   ├── boot.asm        Stage1 MBR — 512B 引导扇区
│   └── stage2.asm      Stage2 — GDT + VBE + 保护模式切换
├── kernel/             内核核心 (C++)
│   ├── kernel.cpp      入口 kernel_main()
│   ├── isr.asm/isr.cpp ISR stub + IsrManager 分发器
│   ├── idt.cpp         IDT 初始化
│   ├── mm.cpp          位图物理内存分配器
│   ├── paging.cpp      PSE 4MB 大页分页
│   ├── task.cpp        Round-Robin 任务调度器
│   ├── syscall.cpp     int 0x80 系统调用
│   ├── loader.cpp      二进制加载器
│   └── user.cpp        TSS + Ring 3 切换
├── drivers/            硬件驱动 (C++ 类)
│   ├── serial.cpp      SerialPort — COM1 串口
│   ├── gfx.cpp         GfxDriver — 1024×768×32bpp 帧缓冲
│   ├── keyboard.cpp    Keyboard — PS/2 键盘
│   ├── mouse.cpp       Mouse — PS/2 鼠标 IRQ12
│   ├── ata.cpp         AtaController — ATA PIO 读写
│   ├── pic.cpp         PicController — 8259A 中断控制器
│   └── pit.cpp         PitTimer — PIT 100Hz 定时器
├── fs/                 文件系统 (C++)
│   └── fat12.cpp       FatFilesystem — FAT12 读写 + 目录操作
├── shell/              交互层 (C++)
│   └── shell.cpp       命令解释器
├── lib/                公共库
│   ├── types.h         u8/u16/u32
│   ├── ports.h         inb/outb/inw/outw
│   ├── heap.cpp        内核堆分配器 (kmalloc/kfree)
│   ├── list.h          双向链表 (list_head)
│   └── cpprt.cpp       C++ 运行时 (operator new/delete)
└── linker.ld           内核链接脚本 (0x20000)
```

---

## 构建

### 依赖

- **NASM** (汇编)
- **Clang++** (C++17，i686-elf target)
- **LLD** / **llvm-objcopy** (链接/提取)
- **Python 3** (镜像合成 + 字体生成)
- **QEMU** (运行，qemu-system-i386)

### 一键构建运行

```bash
python run.py          # 自动清理→编译→构建镜像→启动 QEMU
make                   # 仅编译
make run               # 编译 + QEMU（无串口 stdio）
```

---

## Shell 命令

```
HELP          显示所有命令
INFO          系统信息
MEM           内存统计
CLEAR         清屏
DIR / LS      列出当前目录文件
CAT <name>    查看文件内容
CD <dir>      进入子目录（CD \ 回根）
MKDIR <name>  创建目录
RMDIR <name>  删除空目录
RENAME <a> <b> 重命名文件/目录
CREATE <name> <size> 创建文件
RM <name>     删除文件
ECHO <text>   回显文本
DISK ID       硬盘信息
DISK R <lba>  Hex dump 扇区
MOUSE         显示鼠标坐标
RUN           切换到 Ring 3 用户程序
REBOOT        重启
SHUTDOWN      关机
```

---

## 驱动类架构

所有驱动通过全局实例 + C 兼容包装提供 API：

```cpp
SerialPort    com1(0x3F8);   // serial_write_char → com1.putc
GfxDriver     gfx;           // gfx_puts → gfx.puts
AtaController ata;           // ata_read → ata.read
PicController pic;           // pic_remap → pic.remap
PitTimer      pit;           // pit_init → pit.init
Keyboard      kb;            // kb_getchar → kb.getchar
Mouse         mouse;         // mouse_get → mouse.get
FatFilesystem fat;           // fat_init → fat.init
IsrManager    isr_mgr;       // isr_register → isr_mgr.register_handler
```

---

## 技术栈

| 语言 | 用途 |
|------|------|
| **NASM Assembly** | MBR、Stage2、ISR Stub |
| **C++17 (Clang++)** | 内核核心、驱动、Shell、文件系统 |
| **LLD** | ELF 链接 |
| **Python** | 镜像构建、字体生成 |
| **QEMU** | i386 虚拟机 |

---

## License

MIT
