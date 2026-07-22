#include "kernel/task.h"
#include "kernel/paging.h"
#include "lib/heap.h"
#include "lib/ports.h"
#include "drivers/serial.h"

static u32 next_pid = 1;
static struct task_struct *main_task;

u32 task_next_pid(void) { return next_pid; }

struct list_head ready_queue;
struct task_struct *current_task;

static void task_wrapper(void) {
    current_task->entry(current_task->arg);
    task_exit();
}

void task_init(void) {
    list_init(&ready_queue);
    main_task = (task_struct *)kmalloc(sizeof(struct task_struct));
    main_task->pid = 0;
    main_task->state = TASK_RUNNING;
    main_task->cs  = 0x18;   /* kernel code */
    main_task->kernel_stack = 0;
    main_task->paging = PagingManager::get_kernel_paging();
    main_task->user_stack = 0;
    main_task->parent = nullptr;
    main_task->exit_code = 0;
    list_init(&main_task->children);
    current_task = main_task;
}

int task_create(void (*entry)(void *), void *arg) {
    struct task_struct *t = (task_struct *)kmalloc(sizeof(struct task_struct));
    if (!t) return -1;

    u32 *stack = (u32 *)kmalloc(4096);
    if (!stack) { kfree(t); return -1; }
    for (int i = 0; i < 1024; i++) stack[i] = 0xCCCCCCCC;

    u32 *sp = stack + 1024;

    *(--sp) = 0x202;             // eflags
    *(--sp) = 0x18;              // cs (kernel code selector)
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
    t->cs  = 0x18;    /* kernel code */
    t->esp = (u32)sp;
    t->eflags = 0x202;
    t->state = TASK_READY;
    t->kernel_stack = (u32)stack;
    t->entry = entry;
    t->arg = arg;
    t->paging = PagingManager::get_kernel_paging();
    t->user_stack = 0;
    t->parent = nullptr;
    t->exit_code = 0;
    list_init(&t->children);
    list_add_tail(&t->list, &ready_queue);

    return t->pid;
}

int task_create_user(void *entry, u32 user_stack_top, PagingManager *user_pd) {
    __asm__ volatile("movb $'T', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    struct task_struct *t = (task_struct *)kmalloc(sizeof(struct task_struct));
    if (!t) { __asm__ volatile("movb $'1', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al"); return -1; }

    __asm__ volatile("movb $'K', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    u32 *kstack = (u32 *)kmalloc(4096);
    if (!kstack) { __asm__ volatile("movb $'2', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al"); kfree(t); return -1; }
    __asm__ volatile("movb $'S', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    for (int i = 0; i < 1024; i++) kstack[i] = 0xCCCCCCCC;

    /* The kernel stack top: when entering from ring3 via interrupt,
       the CPU pushes SS, ESP, EFLAGS, CS, EIP onto this stack.
       We set ESP0 in the TSS to point here. */
    u32 *sp = kstack + 1024;

    /* Build initial interrupt frame for returning to user mode via iretd.
       The frame format from top to bottom:
       SS, user_ESP, EFLAGS, CS, EIP, err=0, vec */
    *(--sp) = 0x23;                 // SS (user data selector)
    *(--sp) = user_stack_top;       // ESP
    *(--sp) = 0x202;                // EFLAGS (IF set)
    *(--sp) = 0x2B;                 // CS (user code selector)
    *(--sp) = (u32)entry;           // EIP
    *(--sp) = 0;                    // err_code
    *(--sp) = 0x20;                 // vec (timer, will be overwritten)

    /* pusha slots */
    *(--sp) = 0;                    // eax
    *(--sp) = 0;                    // ecx
    *(--sp) = 0;                    // edx
    *(--sp) = 0;                    // ebx
    sp--;                           // _esp slot
    *sp = (u32)(sp - 3);            // _esp → edi slot
    *(--sp) = 0;                    // ebp
    *(--sp) = 0;                    // esi
    *(--sp) = 0;                    // edi

    t->pid = next_pid++;
    t->ecx = 0; t->edx = 0; t->ebx = 0; t->ebp = 0; t->esi = 0; t->edi = 0;
    t->eip = (u32)entry;
    t->cs  = 0x2B;    /* user code (ring3) */
    t->esp = (u32)sp;
    t->eflags = 0x202;
    t->state = TASK_READY;
    t->kernel_stack = (u32)kstack;
    t->entry = nullptr;
    t->arg = nullptr;
    t->paging = user_pd;
    t->user_stack = user_stack_top;
    t->parent = current_task;    /* parent is whoever created us */
    t->exit_code = 0;
    list_init(&t->children);
    list_add_tail(&t->sibling, &current_task->children);
    list_add_tail(&t->list, &ready_queue);

    current_task = t;
    __asm__ volatile("movb $'U', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    return t->pid;
}

void task_start_user(void) {
    if (!current_task || !current_task->paging) return;

    __asm__ volatile("movb $'>', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    /* Build the ring-3 iret frame on the kernel stack */
    u32 *sp = (u32 *)(current_task->kernel_stack + 4096);
    *(--sp) = 0x23;                        // SS (user data)
    *(--sp) = current_task->user_stack;    // ESP
    *(--sp) = 0x002;                       // EFLAGS (IF=0, no hw interrupts)
    *(--sp) = 0x2B;                        // CS (user code, RPL=3)
    *(--sp) = current_task->eip;           // EIP

    /* Load user page directory */
    __asm__ volatile("movb $'C', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");
    current_task->paging->load();
    __asm__ volatile("movb $'R', %%al; movw $0x3F8, %%dx; outb %%al, %%dx" ::: "dx","al");

    __asm__ volatile(
        "mov %0, %%esp\n"
        "iret\n"
        :
        : "r"(sp)
    );
    __builtin_unreachable();
}

void task_exit(void) {
    current_task->state = TASK_DEAD;
    list_del(&current_task->list);

    /* Wake up parent if it's waiting (waitpid) */
    if (current_task->parent) {
        list_del(&current_task->sibling);
        if (current_task->parent->state == TASK_BLOCKED) {
            current_task->parent->state = TASK_READY;
            list_add_tail(&current_task->parent->list, &ready_queue);
        }
    }

    /* Free user page directory (not kernel's) */
    if (current_task->paging && current_task->paging != PagingManager::get_kernel_paging()) {
        delete current_task->paging;
    }

    kfree((void *)current_task->kernel_stack);
    kfree(current_task);

    /* Switch back to kernel page table and reschedule */
    PagingManager::get_kernel_paging()->load();

    /* Build a fake interrupt frame on the kernel stack to call schedule */
    registers_t fake;
    fake.eflags = 0x202;
    fake.eip = 0;
    fake._esp = 0;
    __asm__ volatile("mov %%esp, %0" : "=m"(fake._esp));
    schedule(&fake);

    /* Should never reach here — schedule() switches away */
    for (;;) __asm__ volatile("hlt");
}

void task_yield(void) {
    __asm__ volatile("int $0x20");
}

void schedule(registers_t *r) {
    if (!current_task) return;

    /* Save current task context (including pid 0 kernel shell) */
    current_task->ecx = r->ecx;
    current_task->edx = r->edx;
    current_task->ebx = r->ebx;
    current_task->ebp = r->ebp;
    current_task->esi = r->esi;
    current_task->edi = r->edi;
    current_task->eip = r->eip;
    current_task->cs  = r->cs;
    current_task->esp = r->_esp;
    current_task->eflags = r->eflags;

    /* Re-queue running tasks (skip DEAD tasks being cleaned up) */
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        list_add_tail(&current_task->list, &ready_queue);
    }

    if (list_empty(&ready_queue)) {
        /* Nothing to run — this shouldn't happen; return to current */
        current_task->state = TASK_RUNNING;
        return;
    }

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
    r->cs  = nt->cs;
    r->_esp = nt->esp;
    r->eflags = nt->eflags;
    r->eax = 0;

    /* CR3 switch: load the new task's page directory */
    if (nt->paging && nt->paging != current_task->paging) {
        nt->paging->load();
    }

    current_task = nt;
}
