#pragma once
#include "lib/types.h"
#include "lib/list.h"
#include "kernel/isr.h"

enum task_state {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_DEAD,
};

class PagingManager;  /* forward declaration */

/* ---- 准则四：Capability Tokens ---- */
#define CAP_DISK_READ    (1 << 0)
#define CAP_DISK_WRITE   (1 << 1)
#define CAP_SCREEN       (1 << 2)   /* write to framebuffer / gfx */
#define CAP_SHUTDOWN     (1 << 3)
#define CAP_SIGNAL       (1 << 4)   /* send signals to other tasks */
#define CAP_SYSCALL      (1 << 5)   /* call any syscall */
#define CAP_ALL          (CAP_DISK_READ | CAP_DISK_WRITE | CAP_SCREEN | \
                          CAP_SHUTDOWN | CAP_SIGNAL | CAP_SYSCALL)

struct task_struct {
    u32 pid;
    u32 ecx, edx, ebx, ebp, esi, edi;
    u32 eip, cs, esp, eflags;
    u8  state;
    struct list_head list;
    u32 kernel_stack;
    void (*entry)(void *);
    void *arg;
    PagingManager *paging;
    u32 user_stack;
    struct task_struct *parent;
    int  exit_code;
    struct list_head children;
    struct list_head sibling;

    /* ---- 准则三：动态优先级调度 ---- */
    u8   priority;        /* base priority 0-255 (higher = more CPU) */
    u8   dynamic_boost;   /* temporary boost from IRQ interaction */
    u32  boost_expire;    /* tick count when boost decays */

    /* ---- 准则四：Capability Tokens ---- */
    u32  caps;            /* capability bitmask */

    /* ---- 准则一：per-task output redirect ---- */
    int  output_fd;       /* -1=screen, >=0=FD for gfx output redirect */

    /* Signal support */
    u32 pending_signals;
    u32 blocked_signals;
    u32 sig_handlers[32];
    u32 sig_saved_eip;
    u32 sig_saved_esp;
};

/* Signal numbers */
#define SIGKILL   9
#define SIGINT    2
#define SIGSEGV  11
#define SIGCHLD  17
#define SIGTERM  15
#define SIG_DFL   0   /* default action */
#define SIG_IGN   1   /* ignore */

extern struct list_head ready_queue;
extern struct task_struct *current_task;

void task_init(void);
int  task_create(void (*entry)(void *), void *arg);
int  task_create_user(void *entry, u32 user_stack_top, PagingManager *user_pd);
void task_start_user(void);   /* switch to user mode via direct iretd */
void task_launch_user(void);   /* save current, build 5-entry iretd to ring3 */
void task_exit(void);
void task_yield(void);
void schedule(registers_t *r);
u32  task_next_pid(void);
int  task_send_signal(u32 pid, int sig);
void task_check_signals(registers_t *r);

/* Capability management (准则四) */
void task_boost_priority(u32 pid, u8 amount);  /* IRQ-triggered boost (准则三) */
void task_decay_boosts(void);                   /* periodic decay (准则三) */
bool task_has_cap(u32 pid, u32 cap);
int  task_drop_cap(u32 cap);                    /* remove capability from self */
