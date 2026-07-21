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
    u32 eip, esp, eflags;
    u8  state;
    struct list_head list;
    u32 kernel_stack;
    void (*entry)(void *);
    void *arg;
    PagingManager *paging;
    u32 user_stack;     /* top of user stack (virt addr) */
};

extern struct list_head ready_queue;
extern struct task_struct *current_task;

void task_init(void);
int  task_create(void (*entry)(void *), void *arg);
int  task_create_user(void *entry, u32 user_stack_top, PagingManager *user_pd);
void task_exit(void);
void task_yield(void);
void schedule(registers_t *r);
