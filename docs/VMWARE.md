# 在 VMware 上运行 XEKernelOS

QEMU 之外的目标平台。启动链（MBR → stage2 → 内核）只依赖 BIOS 中断
（INT 13h 读盘 / INT 10h VBE / INT 15h E820），因此 VMware 只要按
**BIOS 固件 + IDE 硬盘** 配置就能直接跑。SATA/NVMe 与 EFI 都不支持
（内核没有 AHCI / GOP 驱动）。

## 1. 生成 vmdk 与 vmx

```bash
source tools/env.sh          # MSYS2 clang64: nasm/clang/lld/qemu-img
make                         # 产出 build/xekernelos.img, build/disk.img
python tools/mkvmdk.py       # → build/vmware/{XEKernelOS.vmdk, -data.vmdk, .vmx}
```

若 VMware/BIOS 对小于 8MB 的"硬盘"表现异常（识别不到、不进启动流程），
先把镜像补齐再转换：

```bash
python tools/mkvmdk.py --pad-mb 64
```

## 2. 打开虚拟机

用 VMware Workstation / Player 直接打开 `build/vmware/XEKernelOS.vmx`。
模板已经写好全部关键配置，不需要在 GUI 里改硬件：

| 配置 | 值 | 原因 |
|---|---|---|
| `firmware` | `bios` | stage2 用 INT 10h/13h 实模式调用，EFI 下无这些服务 |
| `ide0:0` | `XEKernelOS.vmdk` | 启动盘：MBR + stage2 + 内核（必须是第一块盘，BIOS 从它引导） |
| `ide0:1` | `XEKernelOS-data.vmdk` | 数据盘：FAT12 卷（用户看到的文件系统） |
| `memsize` | 256MB | 内核最低 31MB（内核堆固定在 16–31MB），低于此值会明确报错停机 |
| `mks.enable3d` | FALSE | 关 3D，保证走传统 VBE 线性帧缓冲（内核只认 linear framebuffer） |
| `serial0` | 文件 `serial.log` | 内核日志走 COM1，**排障第一现场** |

> 如果自己新建虚拟机：磁盘类型必须选 **IDE**（新版本 GUI 默认给 SATA/NVMe，
> 需要手工在 .vmx 里加 `ide0:0.present = "TRUE"` 之类的条目）；
> 固件必须选 **BIOS**（不要 EFI）；内存 ≥ 64MB。

## 3. 预期结果

1. VMware 开机约 3 秒后进入图形桌面（1024×768，顶部/底部任务栏 + 6 个图标）。
2. 按 `ESC` 退出桌面，进入用户态 Shell（`XEKernel@Xek\>`）。
3. `serial.log` 里应能看到探测结果：

```
mm: detected RAM 00000100MB       ← E820 探测到的物理内存
VBE: fb=0xE0000000 w=1024 h=0768 bpp=32 pitch=4096
ata: drv0 ident=K sec0=<MBR 字节> fat=n
ata: drv1 ident=K sec0=<EB 3C 90> fat=Y
ata: data drive = 1
```

## 4. 排障

QEMU 与 VMware 的差异集中在三处，代码里都做了适配并打了日志：

| 症状 | 原因 | 日志/处理 |
|---|---|---|
| 开机黑屏，无任何输出 | 视频模式没选到 | stage2 现在**枚举 VBE 模式列表**（不再硬编码 QEMU 的 `0x4144`），按 32bpp > 24bpp、1024×768 加权打分。若一个可用模式都没有，会**切回文本模式打印原因**：`no usable VBE mode (need 24/32bpp, >=640x480, linear FB)` |
| `VBE int 10h/4F00 failed` | 机器/固件没有 VBE | 需要支持 VBE 2.0 的 BIOS；VMware 都支持 |
| 卡在 `stage2` 之后，VMware 日志出现 `I/O out of range` / `out of bounds` | **内核加载循环曾依赖 BIOS 回填 AX**：调用前 `AX=0x42<<8\|请求扇区数`，SeaBIOS 成功时把 AH 清 0（所以 QEMU 正常），VMware 不清 → `sub cx,ax` 下溢、`add [dap_lba],eax` 每轮跳 1.7 万扇区 → 读到盘尾越界（实测 `ide0:0 numIOs=1647`） | 已修：只在**请求值**上推进循环，不看 BIOS 返回值；目标地址改 32 位线性值换算 `seg:off`（顺带去掉内核 >130KB 时的 offset 溢出） |
| 有引导标记但停在 `kernel` | 内核读盘失败 | MBR/stage2 现在都逐行打印进度：`XEKernelOS MBR` → `stage2` → `kernel` → `vbe` → `pm`；最后一行就是失败点 |
| `ERR: Disk!` | 启动盘读不到 | boot.asm 现在用 BIOS 传入的 DL（不再硬编码 0x80）；确认 `.vmdk` 在 IDE 0:0 |
| 进不了 Shell、文件操作失败 | 数据盘没定位到 | `ata: drvN ... fat=Y/n` 会逐盘打印。内核自动选带 FAT 卷的那块（默认沿用从盘，主盘有 FAT 卷时切主盘） |
| `mm: FATAL - RAM below kernel heap end` | 内存 < 31MB | 把虚拟机内存调到 ≥ 64MB |
| 桌面能出但鼠标不动 | PS/2 鼠标不被转发 | 关掉 `usb.present`（已默认关），改用 PS/2；VMware 默认给 BIOS 客户机提供 PS/2 |
| BIOS 画面后只有光标闪烁、连 `XEKernelOS MBR` 都没有 | BIOS 未执行我们的引导扇区 | MBR 现在带一张**活动分区表项**（旧版本分区表全 0，部分 BIOS 直接判为不可引导）；另外每次读盘前加了磁盘控制器 reset。若仍如此，检查虚拟机设置里硬盘是否在 **IDE 0:0**、启动顺序是否 Hard Drive 优先 |

## 5. 与 QEMU 的差异（实测结论）

| 项 | QEMU | VMware | 状态 |
|---|---|---|---|
| VBE 模式号 | 专属 `0x4144` | 各自编号不同 | ✅ 改为枚举 + 打分；已用「强制 24bpp 回退」验证过非硬编码路径 |
| 物理内存 | 硬编码 64MB | 视虚拟机设置 | ✅ stage2 用 E820 探测写入 `0x514`；`-m 32` 实测报 31MB（SeaBIOS 为 ACPI 保留 128KB） |
| 数据盘位置 | 从盘 `0xF0` | 可能主盘 | ✅ 逐盘 IDENTIFY + 读扇区 0 判 FAT BPB，自动选择 |
| 内核加载循环 | SeaBIOS 回填 AL | **不回填** | ✅ 曾因此读到盘尾越界（`ide0:0 numIOs=1647` + `I/O out of range`）。已改为按「本次请求扇区数」推进，不依赖 BIOS 返回值 |
| MBR 分区表 | 全 0 也能引导 | **要求有活动分区项** | ✅ 已补一张活动分区表项（引导代码本身不读它） |
| 串口日志 | TCP | 文件 `serial.log`（逐次写入即落盘） | ✅ 可用；已据此定位多个问题 |
| 24bpp 模式 | 可用 | 常用 | ✅ gfx 本就支持；鼠标光标读写已按 bpp 处理 |

### 5.1 未解决：VMware 上 `iret` 进 ring3 偶发 triple fault

现象：约 2/3 概率在 `loader: flat binary 38964B` 之后弹
"virtual CPU ... shutdown state"（即 guest 三重故障），点 OK 重启后能正常进桌面。

已逐项排除（探针都在 ring0，异常可正常投递，不会静默）：

| 检查 | 结果 |
|---|---|
| 页目录 / ESP0 / 入口 / 用户栈 / CR3 / EFLAGS | 与 QEMU 完全一致 |
| TSS ESP0 目标页可写 | ✅ |
| 用户栈顶页可写 | ✅ |
| `int 0x80` 门（`off/sel/flags`） | ✅ 与 QEMU 逐位一致 |
| ring0 走 `int 0x80`（门 + 分发链路） | ✅ 正常返回 pid |
| GDT 中 TSS 描述符 base/limit、TSS 内 ESP0/SS0、`tr` | ✅ 全部正确 |
| 关键异常/定时器向量 IDT 门（0/6/8/13/14/0x20） | ✅ 全部正确 |
| TSS 页与页目录是否同页 | ✅ 不同页 |
| 串口日志被缓冲截断 | ✅ 排除（两次失败日志字节数不同，非同一缓冲边界） |
| **进 ring3 时关闭中断（IF=0）** | ❌ **仍然 triple fault** → 与中断投递无关 |

结论：故障点在 `iret` 特权切换本身，且 guest 侧状态已逐项验证有效、ring0 异常路径可用，
因此**高度怀疑是 VMware 在 Windows Hypervisor Platform（Hyper-V）后端上对
「PSE 4MB 页 + ring3 特权切换」的处理问题**（`vmware.log` 里有
`Syncing WHP TSCs`，说明它走的是 WHP 而非 VMware 原生 monitor）。

建议的 A/B 验证（任一即可判定）：
1. 关闭 Windows 的「虚拟机平台 / Hyper-V」（`bcdedit /set hypervisorlaunchtype off` + 重启），
   让 VMware 用原生 monitor 再试；
2. 换一个宿主验证同一份镜像：用 `VBoxManage convertfromraw build/xekernelos.img a.vhd`
   在 VirtualBox 里跑，或直接往真机 U 盘写入；
3. 若确认是宿主问题，本内核侧无需改动 —— 也欢迎提供 VMware 版本号与
   `vmware.log` 中故障前后的完整片段继续定位。

**注意**：QEMU 侧全流程（含串口冒烟 10 项断言）始终全绿，上述问题只出现在 VMware/WHP 上。
