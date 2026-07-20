#include "kernel/task.h"
#include "lib/heap.h"
#include "drivers/serial.h"

static u32 next_pid = 1;
static struct task_struct *main_task;

struct list_head ready_queue;
struct task_struct *current_task;

static void task_wrapper(void) {
    current_task->entry(current_task->arg);
    task_exit();
}

void task_init(void) {
    list_init(&ready_queue);
    main_task = kmalloc(sizeof(struct task_struct));
    main_task->pid = 0;
    main_task->state = TASK_RUNNING;
    main_task->kernel_stack = 0;
    current_task = main_task;
}

int task_create(void (*entry)(void *), void *arg) {
    struct task_struct *t = kmalloc(sizeof(struct task_struct));
    if (!t) return -1;

    u32 *stack = kmalloc(4096);
    if (!stack) { kfree(t); return -1; }
    for (int i = 0; i < 1024; i++) stack[i] = 0xCCCCCCCC;

    u32 *sp = stack + 1024;

    *(--sp) = 0x202;             // eflags
    *(--sp) = 0x18;              // cs
    *(--sp) = (u32)task_wrapper; // eip
    *(--sp) = 0;                 // err
    *(--sp) = 0x20;              // vec
    *(--sp) = 0;                 // eax
    *(--sp) = 0;                 // ecx
    *(--sp) = 0;                 // edx
    *(--sp) = 0;                 // ebx
    sp--;                        // sp points to _esp slot
    *sp = (u32)(sp - 3);         // _esp = &stack[1011] (edi)
    *(--sp) = 0;                 // ebp
    *(--sp) = 0;                 // esi
    *(--sp) = 0;                 // edi

    t->pid = next_pid++;
    t->ecx = 0; t->edx = 0; t->ebx = 0; t->ebp = 0; t->esi = 0; t->edi = 0;
    t->eip = (u32)task_wrapper;
    t->esp = (u32)sp;
    t->eflags = 0x202;
    t->state = TASK_READY;
    t->kernel_stack = (u32)stack;
    t->entry = entry;
    t->arg = arg;
    list_add_tail(&t->list, &ready_queue);

    return t->pid;
}

void task_exit(void) {
    current_task->state = TASK_DEAD;
    list_del(&current_task->list);
    kfree((void *)current_task->kernel_stack);
    kfree(current_task);
    for (;;) __asm__ volatile("hlt");
}

void task_yield(void) {
    __asm__ volatile("int $0x20");
}

void schedule(registers_t *r) {
    if (!current_task) return;

    if (current_task->pid != 0) {
        current_task->ecx = r->ecx;
        current_task->edx = r->edx;
        current_task->ebx = r->ebx;
        current_task->ebp = r->ebp;
        current_task->esi = r->esi;
        current_task->edi = r->edi;
        current_task->eip = r->eip;
        current_task->esp = r->_esp;
        current_task->eflags = r->eflags;
    }

    if (current_task->state == TASK_RUNNING && current_task->pid != 0) {
        current_task->state = TASK_READY;
        list_add_tail(&current_task->list, &ready_queue);
    }

    if (list_empty(&ready_queue)) return;

    struct list_head *next = ready_queue.next;
    struct task_struct *nt = container_of(next, struct task_struct, list);
    list_del(next);
    nt->state = TASK_RUNNING;

    r->ecx = nt->ecx;
    r->edx = nt->edx;
    r->ebx = nt->ebx;
    r->ebp = nt->ebp;
    r->esi = nt->esi;
    r->edi = nt->edi;
    r->eip = nt->eip;
    r->_esp = nt->esp;
    r->eflags = nt->eflags;
    r->eax = 0;

    current_task = nt;
}
