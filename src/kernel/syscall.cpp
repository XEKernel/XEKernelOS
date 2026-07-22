#include "kernel/syscall.h"
#include "kernel/user.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/gfx.h"
#include "drivers/mouse.h"
#include "drivers/pit.h"
#include "fs/fat12.h"
#include "lib/heap.h"
#include "lib/ports.h"

#define MAX_FD 4

/* File descriptor table */
static u8  *fd_buf[MAX_FD] = {nullptr};
static u32  fd_size[MAX_FD] = {0};

/* Heap break — starts at 0x10000000 (PDE 64, clear of kernel PDEs) */
static u32 user_break = 0x10000000;

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    if (!str || len > 4096) { r->eax = (u32)-1; return; }
    serial_write_str_len(str, len);
    serial_write_char('\n');
    r->eax = len;
}

static void sys_read(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (max <= 0 || max > 4096 || !buf) { r->eax = 0; return; }
    kb_readline(buf, max - 1);
    buf[max - 1] = 0;
    int n = 0; while (buf[n]) n++;
    r->eax = n;
}

static void sys_open(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }

    /* Find free fd slot */
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_buf[i]) { fd = i; break; }
    }
    if (fd < 0) { r->eax = (u32)-1; return; }

    u8 *fb = (u8 *)kmalloc(65536);
    if (!fb) { r->eax = (u32)-1; return; }

    int sz = fat_read_file_buf(name, fb, 65536);
    if (sz <= 0) { kfree(fb); r->eax = (u32)-1; return; }

    fd_buf[fd] = fb;
    fd_size[fd] = (u32)sz;
    r->eax = fd;
}

static void sys_fread(registers_t *r) {
    u32 fd = r->ebx;
    char *buf = (char *)r->ecx;
    u32 len = r->edx;

    if (fd >= MAX_FD || !fd_buf[fd] || !buf) { r->eax = (u32)-1; return; }
    if (len > fd_size[fd]) len = fd_size[fd];
    if (len > 4096) len = 4096;
    for (u32 i = 0; i < len; i++) buf[i] = fd_buf[fd][i];
    r->eax = len;
}

static void sys_sbrk(registers_t *r) {
    u32 bytes = r->ebx;
    if (bytes == 0) { r->eax = user_break; return; }

    u32 pages = (bytes + 0xFFF) / 0x1000;
    u32 old_break = user_break;

    if (!g_user_pd) { r->eax = (u32)-1; return; }

    for (u32 i = 0; i < pages; i++) {
        u32 phys = mm_alloc_page();
        if (!phys) { r->eax = (u32)-1; return; }
        g_user_pd->map_page(user_break, phys, PT_FLAGS);
        user_break += 0x1000;
    }

    r->eax = old_break;
}

static void sys_getcwd(registers_t *r) {
    char *buf = (char *)r->ebx;
    int max = (int)r->ecx;
    if (!buf || max <= 0 || max > 256) { r->eax = (u32)-1; return; }
    fat.cwd_str(buf, max);
    int n = 0; while (buf[n]) n++;
    r->eax = n;
}

static void sys_time(registers_t *r) {
    char *buf = (char *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    auto bcd = [](u8 v) -> u8 { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); };
    outb(0x70, 0x04); u8 h = bcd(inb(0x71));
    outb(0x70, 0x02); u8 m = bcd(inb(0x71));
    outb(0x70, 0x00); u8 s = bcd(inb(0x71));
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10); buf[5] = ':';
    buf[6] = '0' + (s / 10); buf[7] = '0' + (s % 10); buf[8] = 0;
    r->eax = 8;
}

/* ---- FAT filesystem syscalls for user-space shell ---- */

static void sys_fat_dir(registers_t *r) {
    fat.dir();
    r->eax = 0;
}

static void sys_fat_cd(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }
    r->eax = fat.cd(name);
}

static void sys_fat_mkdir(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }
    r->eax = fat.mkdir(name);
}

static void sys_fat_rmdir(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }
    r->eax = fat.rmdir(name);
}

static void sys_fat_delete(registers_t *r) {
    const char *name = (const char *)r->ebx;
    if (!name) { r->eax = (u32)-1; return; }
    r->eax = fat.delete_file(name);
}

static void sys_fat_rename(registers_t *r) {
    const char *old_name = (const char *)r->ebx;
    const char *new_name = (const char *)r->ecx;
    if (!old_name || !new_name) { r->eax = (u32)-1; return; }
    r->eax = fat.rename(old_name, new_name);
}

static void sys_fat_write(registers_t *r) {
    const char *name = (const char *)r->ebx;
    const u8 *data = (const u8 *)r->ecx;
    u32 size = r->edx;
    if (!name || !data || size > 4096) { r->eax = (u32)-1; return; }
    r->eax = fat.write_file(name, data, size);
}

/* ---- fork: clone current task with copied address space ---- */

static void sys_fork(registers_t *r) {
    if (!current_task || current_task->pid == 0) {
        r->eax = (u32)-1;
        return;
    }

    PagingManager *parent_pd = current_task->paging;
    if (!parent_pd) { r->eax = (u32)-1; return; }

    PagingManager *child_pd = new PagingManager();

    /* Copy user-space page tables (PDE 32+), skipping 4MB PSE pages */
    for (int pde = 32; pde < 1024; pde++) {
        u32 src_pde = parent_pd->get_pde(pde);
        if (!(src_pde & 1)) continue;
        if (src_pde & 0x80) continue;
        u32 *src_pt = (u32 *)(src_pde & 0xFFFFF000);
        for (int pte = 0; pte < 1024; pte++) {
            u32 entry = src_pt[pte];
            if (!(entry & 1)) continue;
            u32 new_phys = mm_alloc_page();
            if (!new_phys) { delete child_pd; r->eax = (u32)-1; return; }
            u32 *s = (u32 *)(entry & 0xFFFFF000), *d = (u32 *)new_phys;
            for (int k = 0; k < 1024; k++) d[k] = s[k];
            child_pd->map_page((pde << 22) | (pte << 12), new_phys, entry & 0xFFF);
        }
    }

    task_struct *child = (task_struct *)kmalloc(sizeof(task_struct));
    u32 *kstack = (u32 *)kmalloc(4096);
    if (!child || !kstack) {
        if (child) kfree(child);
        delete child_pd;
        r->eax = (u32)-1;
        return;
    }

    child->pid   = task_next_pid();
    child->ecx   = r->ecx;  child->edx   = r->edx;
    child->ebx   = r->ebx;  child->ebp   = r->ebp;
    child->esi   = r->esi;  child->edi   = r->edi;
    child->eip   = r->eip;  child->cs    = r->cs;
    child->eflags = r->eflags;
    child->esp   = r->_esp;
    child->state = TASK_READY;
    child->kernel_stack = (u32)kstack;
    child->entry = nullptr;  child->arg = nullptr;
    child->paging = child_pd;
    child->user_stack = current_task->user_stack;
    child->parent = current_task;
    child->exit_code = 0;
    list_init(&child->children);
    list_add_tail(&child->sibling, &current_task->children);
    list_add_tail(&child->list, &ready_queue);

    /* Copy kernel stack frame to child */
    u32 *psp = (u32 *)(current_task->kernel_stack + 4096);
    u32 *csp = (u32 *)(child->kernel_stack + 4096);
    int frame_words = (int)(psp - (u32 *)r);
    for (int i = 0; i < frame_words; i++)
        csp[-i - 1] = psp[-i - 1];

    /* Child's eax = 0, parent's eax = child PID */
    registers_t *cr = (registers_t *)(csp - frame_words);
    cr->eax = 0;
    child->esp = (u32)cr;

    r->eax = child->pid;
}

static void sys_exec(registers_t *r) {
    const char *path = (const char *)r->ebx;
    if (!path) { r->eax = (u32)-1; return; }

    /* Read ELF binary */
    u8 *elf_buf = (u8 *)kmalloc(65536);
    if (!elf_buf) { r->eax = (u32)-1; return; }
    int sz = fat.read_file(path, elf_buf, 65536);
    if (sz <= 0) { kfree(elf_buf); r->eax = (u32)-1; return; }

    /* Replace current task's address space */
    PagingManager *old_pd = current_task->paging;
    if (old_pd && old_pd != PagingManager::get_kernel_paging())
        delete old_pd;

    PagingManager *new_pd = new PagingManager();
    current_task->paging = new_pd;

    /* Load ELF into new address space */
    /* Simple flat binary: load at 0x400000, stack at 0x9E000 */
    u32 load_addr = 0x400000;
    u32 entry = 0x400000;
    for (u32 i = 0; i < (u32)sz; i += 0x1000) {
        u32 phys = mm_alloc_page();
        if (!phys) { kfree(elf_buf); r->eax = (u32)-1; return; }
        u32 chunk = (u32)sz - i;
        if (chunk > 0x1000) chunk = 0x1000;
        u32 *d = (u32 *)phys;
        for (u32 k = 0; k < chunk; k++)
            ((u8 *)d)[k] = elf_buf[i + k];
        new_pd->map_page(load_addr + i, phys, PT_FLAGS);
    }

    kfree(elf_buf);

    /* Update task context for new program */
    u32 *csp = (u32 *)(current_task->kernel_stack + 4096);
    /* Build new iret frame: user mode, new entry point */
    *(--csp) = 0x23;         /* SS */
    *(--csp) = 0x9E000;      /* ESP */
    *(--csp) = 0x202;        /* EFLAGS */
    *(--csp) = 0x2B;         /* CS */
    *(--csp) = entry;        /* EIP */
    *(--csp) = 0;            /* err_code */
    *(--csp) = 0x20;         /* vec */
    *(--csp) = 0;            /* eax */
    *(--csp) = 0;            /* ecx */
    *(--csp) = 0;            /* edx */
    *(--csp) = 0;            /* ebx */
    csp--;
    *csp = (u32)(csp - 3);   /* _esp */
    *(--csp) = 0;            /* ebp */
    *(--csp) = 0;            /* esi */
    *(--csp) = 0;            /* edi */

    current_task->eip = entry;
    current_task->esp = (u32)csp;
    current_task->eflags = 0x202;
    current_task->user_stack = 0x9E000;

    /* Rewrite our own interrupt frame so we jump to new program on iret */
    r->eip = entry;
    r->cs = 0x2B;
    r->eflags = 0x202;
    r->eax = 0;
    r->ecx = 0; r->edx = 0; r->ebx = 0;
    r->ebp = 0; r->esi = 0; r->edi = 0;
}

static void sys_waitpid(registers_t *r) {
    /* Block until a child exits */
    struct list_head *pos;
    struct task_struct *dead_child = nullptr;

    /* Check if any child has already exited */
    list_for_each(pos, &current_task->children) {
        struct task_struct *c = container_of(pos, struct task_struct, sibling);
        if (c->state == TASK_DEAD) {
            dead_child = c;
            break;
        }
    }

    if (!dead_child) {
        /* No dead children yet — block and wait */
        current_task->state = TASK_BLOCKED;
        schedule(r);
        /* When we resume, a child has exited */
        /* Find the now-dead child */
        list_for_each(pos, &current_task->children) {
            struct task_struct *c = container_of(pos, struct task_struct, sibling);
            if (c->state == TASK_DEAD) {
                dead_child = c;
                break;
            }
        }
    }

    if (dead_child) {
        r->eax = dead_child->pid;
        /* Clean up the dead child's resources */
        list_del(&dead_child->sibling);
        if (dead_child->paging && dead_child->paging != PagingManager::get_kernel_paging())
            delete dead_child->paging;
        if (dead_child->kernel_stack)
            kfree((void *)dead_child->kernel_stack);
        kfree(dead_child);
    } else {
        r->eax = (u32)-1;
    }
}

static void sys_close(registers_t *r) {
    u32 fd = r->ebx;
    if (fd >= MAX_FD || !fd_buf[fd]) { r->eax = (u32)-1; return; }
    kfree(fd_buf[fd]);
    fd_buf[fd] = nullptr;
    fd_size[fd] = 0;
    r->eax = 0;
}

static void sys_getfb(registers_t *r) {
    /* Fill user buffer with {fb_addr, w, h, pitch, bpp} (5 × u32) */
    u32 *buf = (u32 *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    buf[0] = (u32)gfx.fb_addr();
    buf[1] = (u32)gfx.fb_width();
    buf[2] = (u32)gfx.fb_height();
    buf[3] = (u32)gfx.fb_pitch();
    buf[4] = (u32)gfx.fb_bpp();
    r->eax = 5;  /* number of u32 values written */
}

static void sys_mouse(registers_t *r) {
    u32 *buf = (u32 *)r->ebx;
    if (!buf) { r->eax = (u32)-1; return; }
    int x, y, btn;
    mouse_get(&x, &y, &btn);
    buf[0] = (u32)x;
    buf[1] = (u32)y;
    buf[2] = (u32)btn;
    r->eax = 3;
}

static void sys_sleep(registers_t *r) {
    u32 ms = r->ebx;
    if (ms > 60000) ms = 60000;  /* cap at 1 minute */
    /* PIT ticks at 100Hz — 1 tick = 10ms */
    u32 target = pit.ticks() + ms / 10 + 1;
    while (pit.ticks() < target) {
        __asm__ volatile("sti; hlt; cli");
    }
    r->eax = ms;
}

static void sys_cls(registers_t *r) {
    u8 color = (u8)r->ebx;  /* palette index or BGRA blue byte */
    gfx_clear(color);
    r->eax = 0;
}

static void sys_gfx_putc(registers_t *r) {
    gfx.putc((char)r->ebx);
    r->eax = 0;
}

static void sys_gfx_puts(registers_t *r) {
    gfx.puts_utf8((const char *)r->ebx);
    r->eax = 0;
}

static void sys_gfx_set_fg(registers_t *r) {
    gfx.set_fg((u8)r->ebx);
    r->eax = 0;
}

extern "C" void syscall_handler(registers_t *r) {

    switch (r->eax) {
    case SYS_WRITE: sys_write(r); break;
    case SYS_READ:  sys_read(r);  break;
    case SYS_OPEN:  sys_open(r);  break;
    case SYS_FREAD: sys_fread(r); break;
    case SYS_SBRK:  sys_sbrk(r);  break;
    case SYS_GETCWD: sys_getcwd(r); break;
    case SYS_TIME:  sys_time(r);  break;
    case SYS_GETFB: sys_getfb(r); break;
    case SYS_CLOSE: sys_close(r); break;
    case SYS_MOUSE: sys_mouse(r); break;
    case SYS_SLEEP: sys_sleep(r); break;
    case SYS_CLS:   sys_cls(r);   break;
    case SYS_GFX_PUTC: sys_gfx_putc(r); break;
    case SYS_GFX_PUTS: sys_gfx_puts(r); break;
    case SYS_GFX_SET_FG: sys_gfx_set_fg(r); break;
    case SYS_FAT_DIR:    sys_fat_dir(r);    break;
    case SYS_FAT_CD:     sys_fat_cd(r);     break;
    case SYS_FAT_MKDIR:  sys_fat_mkdir(r);  break;
    case SYS_FAT_RMDIR:  sys_fat_rmdir(r);  break;
    case SYS_FAT_DELETE: sys_fat_delete(r); break;
    case SYS_FAT_RENAME: sys_fat_rename(r); break;
    case SYS_FAT_WRITE:  sys_fat_write(r);  break;
    case SYS_FORK:       sys_fork(r);       break;
    case SYS_EXEC:       sys_exec(r);       break;
    case SYS_WAITPID:    sys_waitpid(r);    break;
    case SYS_EXIT:
        /* Clean up FD table, then return to kernel shell via g_entry_esp */
        for (int i = 0; i < MAX_FD; i++) {
            if (fd_buf[i]) { kfree(fd_buf[i]); fd_buf[i] = nullptr; fd_size[i] = 0; }
        }
        PagingManager::get_kernel_paging()->load();
        __asm__ volatile(
            "mov %0, %%esp\n"
            "pop %%ebp\n"
            "ret\n"
            :
            : "m"(g_entry_esp)
        );
        __builtin_unreachable();
    default:
        r->eax = (u32)-1;
        break;
    }
}
