/* SPIN.BIN — 抢占式调度 + 信号终止 验证程序

   父进程: 纯 CPU 死循环, 循环体内不发起任何 syscall —
           只有 PIT 时间片抢占才能把 CPU 让给别人。
   子进程: 每 ~150ms 打印一行, 3 次后打印 done 并退出。
           若调度不是抢占式的, 父进程会永久饿死子进程
           → 串口里不会出现 "SPIN: child" 行。
   子进程退出后只剩父进程自旋, 此时 Ctrl+C (SIGINT) 应终止它;
   否则被 waitpid 阻塞的 Shell 永远回不来。 */

#include "usys.h"

extern "C" void _start(void) {
    syscall4(SYS_WRITE, (int)"SPIN: start\n", 12, 0);

    int pid = syscall0(SYS_FORK);
    if (pid == 0) {
        /* 子进程 — 唯一证明父进程被抢占的证据。
           顺带验证 SYS_TASK_LIST (GUI 任务管理器数据源) 可从 ring3 调用 */
        struct tinfo { unsigned int pid, state, priority, ring3; } tl[16];
        int nt = syscall4(SYS_TASK_LIST, (int)tl, 16, 0);
        char msg[24]; const char *p = "SPIN: tasks="; int i = 0;
        while (p[i]) { msg[i] = p[i]; i++; }
        if (nt > 0 && nt < 10) msg[i++] = (char)('0' + nt);
        else                   msg[i++] = (nt == 0) ? '0' : '?';
        msg[i++] = '\n';
        syscall4(SYS_WRITE, (int)msg, i, 0);
        for (int k = 0; k < 3; k++) {
            syscall4(SYS_WRITE, (int)"SPIN: child tick\n", 17, 0);
            syscall1(SYS_SLEEP, 150);
        }
        syscall4(SYS_WRITE, (int)"SPIN: child done\n", 17, 0);
        syscall1(SYS_EXIT, 0);
        for (;;) {}
    }

    /* 父进程 — 死循环, 无 syscall */
    volatile unsigned int x = 0;
    for (;;) { x = x + 1; }
}
