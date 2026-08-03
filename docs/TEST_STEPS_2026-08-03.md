# XEKernelOS 手动测试步骤 (2026-08-03)

> 分工：代码/调试信息由 AI 负责，测试由你执行。
> 构建产物已就绪：`build/xekernelos.img` + `build/disk.img`（零警告零错误）。

---

## 0. 环境准备

```bash
# 工具链 PATH（每次新开终端执行一次）
export PATH="/f/MSYS2/clang64/bin:$PATH"
cd /d/project/OS

# 若代码有改动，先重新构建
mingw32-make
```

---

## 1. 测试 A：窗口模式 + 真实键盘（验证键盘 IRQ1 中断驱动）

**目的**：验证中断驱动键盘改造是否生效 —— IRQ1 触发时串口打印 `[KBD] irq1_count=...`。

### 步骤

1. 运行：`./run_test.bat`（串口日志落盘 `build/serial.log`，QEMU 窗口打开）
2. 等 2~3 秒，看到 Shell 提示符 `XEKernel@Xek/>`
3. **测试 1 - 快速输入**：连续输入 `CREATE TEST1.TXT HELLO` 后回车（不要停顿）
4. **测试 2 - 正常操作**：依次执行：
   - `LS`（应看到目录列表）
   - `CREATE TEST2.TXT WORLD`
   - `CAT TEST1.TXT`（应显示 `HELLO`）
   - `CAT TEST2.TXT`（应显示 `WORLD`）
   - `RUN GFXDEMO.BIN`（应画图，Shell 不重启）
   - `ECHO AFTER`（Shell 恢复后能输出）
5. 关闭 QEMU 窗口

### 结果判定

| 检查点 | 通过标准 |
|--------|---------|
| 串口日志 `[KBD] irq1_count=` | 每敲几个键出现一次（说明 IRQ1 中断在触发） |
| 串口日志 `[READ] "..."` | 每条命令后出现，内容与输入**完全一致**（说明无丢键） |
| `CREATE TEST1` + `CAT TEST1` | 能显示 HELLO（BUG#1 修复生效，文件数据不损坏） |
| 两个 CREATE 后 `LS` | TEST1.TXT 和 TEST2.TXT 都在（簇不互相覆盖） |
| `RUN GFXDEMO.BIN` | 屏幕画图 + 回车后 Shell 恢复（BUG#2/3/6 修复生效） |

**如果测试 A 失败**：把 `build/serial.log` 发给我，重点看有没有 `[KBD]` 和 `[READ]` 行。

---

## 2. 测试 B：串口注入模式（验证串口桥 + 命令注入）

**目的**：验证 `-serial tcp` 模式下串口字节桥接为键盘输入的路径（自动化测试基础设施）。

### 步骤

1. 开两个终端：
   - 终端 1：`./run_test.bat 7777`（串口走 TCP，QEMU 窗口打开）
   - 终端 2：`/f/Python/python.exe tools/serial_client.py 7777`
2. 终端 2 出现 `CMD>` 提示后，输入命令测试：
   ```
   HELP
   LS
   CREATE SERIAL1.TXT HELLO
   CAT SERIAL1.TXT
   ECHO FROM-SERIAL
   ```
3. 每个命令输入后回车，观察 QEMU 窗口屏幕输出
4. `quit` 退出客户端，关闭 QEMU

### 结果判定

| 检查点 | 通过标准 |
|--------|---------|
| 终端 2 显示 `[SER] === XEKernelOS boot ===` | 串口连接成功且能收到内核日志 |
| 输入 `HELP` 后屏幕显示帮助 | 串口命令注入生效 |
| `CREATE SERIAL1.TXT HELLO` + `CAT` | 文件创建+读取正常 |
| 客户端显示 `[SER] [READ] "CREATE SERIAL1.TXT HELLO" len=24` | 内核收到完整命令 |

**如果测试 B 失败**：把终端 2 的完整输出发给我。

---

## 3. 自动化冒烟（可选，全自动）

```bash
export PATH="/f/MSYS2/clang64/bin:$PATH"
cd /d/project/OS
/f/Python/python.exe tools/qemu_auto.py
```

预期输出最后一行：`=== 冒烟测试全部通过 ===`

**注意**：自动化脚本走串口 TCP 注入，不依赖 QEMU sendkey（QEMU 11.x 的 sendkey 不触发 PS/2 中断，已确认是模拟器行为差异）。

---

## 4. 调试信息速查

本次新增的串口调试标记：

| 标记 | 含义 |
|------|------|
| `[KBD] irq1_count=64` | IRQ1 中断已触发 64 次（每 64 次打印一次） |
| `[READ] "命令" len=N` | 内核从键盘/串口收到完整命令行 |
| `KB cfg=0x45` | PS/2 配置字节（bit0=1 表示 IRQ1 启用） |

> 若 `[KBD]` 一直不出现但 `[READ]` 正常：说明走的是轮询路径（QEMU sendkey 注入场景），真实键盘窗口模式应触发 IRQ1。
