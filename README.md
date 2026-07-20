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
| **文件系统** | FAT12 读取(BPB 解析、根目录、簇链) |
| **用户态** | Ring 3 用户模式 + TSS |
| **Shell** | 12 个命令，彩色文本终端 |

---

## 项目结构

```
src/
├── boot/              引导层 (NASM)
│   ├── boot.asm        Stage1 MBR — 512B 引导扇区
│   └── stage2.asm      Stage2 — GDT + VBE + 保护模式切换
├── kernel/             内核核心
│   ├── kernel.c        入口 main()
│   ├── isr.asm/isr.c   ISR stub + C 分发器
│   ├── idt.c           IDT 初始化
│   ├── mm.c            位图物理内存分配器
│   ├── paging.c        PSE 4MB 大页分页
│   └── user.c          TSS + Ring 3 切换
├── drivers/            硬件驱动
│   ├── gfx.c           1024×768×32bpp 帧缓冲
│   ├── keyboard.c      PS/2 键盘(轮询+中断)
│   ├── mouse.c         PS/2 鼠标 IRQ12
│   ├── ata.c           ATA PIO 读写
│   ├── pic.c           8259A PIC 重映射
│   └── pit.c           PIT 100Hz 定时器
├── fs/                 文件系统
│   └── fat12.c         FAT12 读取(LS/DIR/CAT/CD/MKDIR)
├── shell/              交互层
│   └── shell.c         命令解释器
├── lib/                公共库
│   ├── types.h         u8/u16/u32
│   └── ports.h         inb/outb/inw
└── linker.ld           内核链接脚本(0x20000)
```

---

## 构建

### 依赖

- **NASM** (汇编)
- **Clang** (C 编译，i686-elf target)
- **LLD** / **llvm-objcopy** (链接/提取)
- **Python 3** (镜像合成 + 字体生成)
- **QEMU** (运行，qemu-system-i386)

### 构建命令

```bash
nasm -f bin src/boot/boot.asm     -o build/boot.bin
nasm -f bin src/boot/stage2.asm   -o build/stage2.bin
nasm -f elf32 src/kernel/isr.asm  -o build/isr_stub.o
clang -target i686-elf -ffreestanding -nostdlib -O1 \
      -mno-sse -mno-mmx -mno-sse2 -I src \
      -c src/kernel/*.c src/drivers/*.c src/fs/*.c src/shell/*.c
ld.lld -m elf_i386 -T src/linker.ld build/*.o -o build/kernel.elf
llvm-objcopy -O binary build/kernel.elf build/kernel.bin
python build_img.py build
```

### 一键运行

双击 `run.bat` 或执行：

```bash
qemu-system-i386 -fda build/xekernelos.img -hda build/disk.img -m 32 -boot order=a
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
CD \<dir>     进入子目录（CD \ 回根）
MKDIR <name>  创建目录
ECHO <text>   回显文本
DISK ID       硬盘信息
DISK R <lba>  Hex dump 扇区
MOUSE         显示鼠标坐标
RUN           切换到 Ring 3
REBOOT        重启
SHUTDOWN      关机
```

---

## 技术栈

| 语言 | 用途 |
|------|------|
| **NASM Assembly** | MBR、Stage2、ISR Stub |
| **C (Clang)** | 内核核心、驱动、Shell、文件系统 |
| **LLD** | ELF 链接 |
| **Python** | 镜像构建、字体生成 |
| **QEMU** | i386 虚拟机 |

---

## License

MIT
