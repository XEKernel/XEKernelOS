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

    /* Signal support */
    u32 pending_signals;       /* bitmap of pending signals (bits 1-31) */
    u32 blocked_signals;       /* blocked signal mask */
    u32 sig_handlers[32];      /* user-space handler addresses (0=SIG_DFL) */
    u32 sig_saved_eip;         /* saved user EIP for sigreturn */
    u32 sig_saved_esp;         /* saved user ESP for sigreturn */
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
void task_check_signals(registers_t *r);     /* for fork */
