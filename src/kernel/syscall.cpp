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
#include "fs/ramdisk.h"
#include "lib/heap.h"
#include "lib/ports.h"

#define MAX_FD 16
#define PIPE_BUF_SZ 4096

/* Pipe ring buffer shared by read/write FDs */
struct pipe_t {
    u8   buf[PIPE_BUF_SZ];
    u32  rpos;    /* read position */
    u32  wpos;    /* write position */
    u32  count;   /* bytes available to read */
    int  refs;    /* reference count (2 when both ends open) */
};

/* File descriptor table */
static u8  *fd_buf[MAX_FD] = {nullptr};
static u32  fd_size[MAX_FD] = {0};
static u32  fd_pos[MAX_FD]  = {0};    /* current read/write position */
static u8   fd_type[MAX_FD] = {0};    /* 0=unused, 1=file, 2=pipe-read, 3=pipe-write */

/* Heap break — starts at 0x10000000 (PDE 64, clear of kernel PDEs) */
static u32 user_break = 0x10000000;

/* Output redirect: when >=0, gfx_puts/putc write to this FD instead of screen */
static int g_output_fd = -1;

static void sys_write(registers_t *r) {
    char *str = (char *)r->ebx;
    u32 len = r->ecx;
    if (!str || len > 4096) { r->eax = (u32)-1; return; }
    serial_write_str_len(str, len);
    serial_write_char('\n');
    r->eax = len;
}

static void sys_fwrite(registers_t *r) {
    u32 fd  = r->ebx;
    char *str = (char *)r->ecx;
    u32 len = r->edx;
    if (len > 4096) len = 4096;

    if (fd >= MAX_FD || !fd_buf[fd]) { r->eax = (u32)-1; return; }

    u8 typ = fd_type[fd];
    if (typ == 3) {  /* pipe write-end */
        pipe_t *pipe = (pipe_t *)fd_buf[fd];
        u32 avail = PIPE_BUF_SZ - pipe->count;
        if (len > avail) len = avail;
        if (len == 0) { r->eax = 0; return; }
        for (u32 i = 0; i < len; i++) {
            pipe->buf[pipe->wpos] = (u8)(str ? str[i] : 0);
            pipe->wpos = (pipe->wpos + 1) % PIPE_BUF_SZ;
        }
        pipe->count += len;
        r->eax = len;
        return;
    }

    if (typ == 1) {  /* file: append */
        u32 space = fd_size[fd] - fd_pos[fd];
        if (len > space) len = space;
        if (len == 0) { r->eax = 0; return; }
        for (u32 i = 0; i < len; i++)
            fd_buf[fd][fd_pos[fd] + i] = (u8)(str ? str[i] : 0);
        fd_pos[fd] += len;
        r->eax = len;
        return;
    }

    r->eax = (u32)-1;
}

static void sys_read(registers_t *r) {
    /* Support both legacy (ebx=buf, ecx=max) and FD-based:
       if ebx < 256 and fd_buf[ebx] is a pipe, read from pipe. */
    u32 fd_or_buf = r->ebx;
    int max = (int)r->ecx;

    /* FD-based pipe read: ebx=fd */
    if (fd_or_buf < MAX_FD && fd_buf[fd_or_buf] && fd_type[fd_or_buf] == 2) {
        pipe_t *pipe = (pipe_t *)fd_buf[fd_or_buf];
        char *buf = (char *)r->ecx;
        if (!buf || max <= 0 || max > 4096) { r->eax = 0; return; }
        u32 n = pipe->count;
        if (n > (u32)max) n = (u32)max;
        if (n == 0) { r->eax = 0; return; }  /* empty pipe */
        for (u32 i = 0; i < n; i++) {
            buf[i] = pipe->buf[pipe->rpos];
            pipe->rpos = (pipe->rpos + 1) % PIPE_BUF_SZ;
        }
        pipe->count -= n;
        r->eax = n;
        return;
    }

    /* Legacy: keyboard readline */
    char *buf = (char *)r->ebx;
    if (max <= 0 || max > 4096 || !buf) { r->eax = 0; return; }
    kb_readline(buf, max - 1);
    buf[max - 1] = 0;
    int n = 0; while (buf[n]) n++;
    __asm__ volatile("wbinvd");
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
    fd_pos[fd] = 0;
    fd_type[fd] = 1;  /* file */
    r->eax = fd;
}

static void sys_fread(registers_t *r) {
    u32 fd = r->ebx;
    char *buf = (char *)r->ecx;
    u32 len = r->edx;

    if (fd >= MAX_FD || !fd_buf[fd] || !buf) { r->eax = (u32)-1; return; }
    u32 remain = fd_size[fd] - fd_pos[fd];
    if (len > remain) len = remain;
    if (len > 4096) len = 4096;
    for (u32 i = 0; i < len; i++) buf[i] = fd_buf[fd][fd_pos[fd] + i];
    fd_pos[fd] += len;
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

    /* Read binary from disk */
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

    /* Load flat binary: copy to new physical pages at 0x400000 */
    u32 load_addr = 0x400000;
    u32 entry = 0x400000;
    u32 code_pages = (sz + 0xFFF) / 0x1000;
    for (u32 i = 0; i < code_pages; i++) {
        u32 phys = mm_alloc_page();
        if (!phys) { kfree(elf_buf); r->eax = (u32)-1; return; }
        u32 chunk = sz - i * 0x1000;
        if (chunk > 0x1000) chunk = 0x1000;
        u8 *d = (u8 *)phys;
        for (u32 k = 0; k < chunk; k++)
            d[k] = elf_buf[i * 0x1000 + k];
        new_pd->map_page(load_addr + i * 0x1000, phys, PT_FLAGS);
    }

    /* Map user stack: 0x410000–0x420000 (64KB) */
    u32 stack_top = 0x420000;
    u32 stack_base = stack_top - 0x10000;
    for (u32 va = stack_base; va < stack_top; va += 0x1000) {
        u32 phys = mm_alloc_page();
        if (!phys) { kfree(elf_buf); r->eax = (u32)-1; return; }
        new_pd->map_page(va, phys, PT_FLAGS);
    }

    kfree(elf_buf);

    /* Build new iret frame on kernel stack */
    u32 *csp = (u32 *)(current_task->kernel_stack + 4096);
    *(--csp) = 0x23;         /* SS */
    *(--csp) = stack_top;    /* ESP */
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
    current_task->user_stack = stack_top;

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

    if (fd_type[fd] == 2 || fd_type[fd] == 3) {
        /* Pipe: decrement refcount, only free when both ends closed */
        pipe_t *pipe = (pipe_t *)fd_buf[fd];
        pipe->refs--;
        if (pipe->refs <= 0)
            kfree(pipe);
    } else {
        kfree(fd_buf[fd]);
    }

    fd_buf[fd] = nullptr;
    fd_size[fd] = 0;
    fd_pos[fd] = 0;
    fd_type[fd] = 0;
    r->eax = 0;
}

static void sys_lseek(registers_t *r) {
    u32 fd = r->ebx;
    int offset = (int)r->ecx;
    int whence = (int)r->edx;
    if (fd >= MAX_FD || !fd_buf[fd]) { r->eax = (u32)-1; return; }
    u32 new_pos;
    if (whence == 0) new_pos = (u32)offset;
    else if (whence == 1) new_pos = fd_pos[fd] + (u32)offset;
    else if (whence == 2) new_pos = fd_size[fd] + (u32)offset;
    else { r->eax = (u32)-1; return; }
    if (new_pos > fd_size[fd]) new_pos = fd_size[fd];
    fd_pos[fd] = new_pos;
    r->eax = new_pos;
}

static void sys_stat(registers_t *r) {
    const char *path = (const char *)r->ebx;
    u32 *buf = (u32 *)r->ecx; /* {size, flags(0=file,1=dir), 0, 0} */
    if (!path || !buf) { r->eax = (u32)-1; return; }
    int sz, is_dir;
    sz = fat.stat(path, &is_dir);
    if (sz < 0) { r->eax = (u32)-1; return; }
    buf[0] = (u32)sz;
    buf[1] = is_dir ? 1u : 0u;
    buf[2] = 0;
    buf[3] = 0;
    r->eax = 0;
}

static void sys_dup(registers_t *r) {
    u32 old_fd = r->ebx;
    if (old_fd >= MAX_FD || !fd_buf[old_fd]) { r->eax = (u32)-1; return; }
    /* Find lowest free fd */
    int new_fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_buf[i]) { new_fd = i; break; }
    }
    if (new_fd < 0) { r->eax = (u32)-1; return; }
    fd_buf[new_fd] = fd_buf[old_fd];
    fd_size[new_fd] = fd_size[old_fd];
    fd_pos[new_fd] = fd_pos[old_fd];
    fd_type[new_fd] = fd_type[old_fd];
    r->eax = new_fd;
}

static void sys_dup2(registers_t *r) {
    u32 old_fd = r->ebx;
    u32 new_fd = r->ecx;
    if (old_fd >= MAX_FD || new_fd >= MAX_FD || !fd_buf[old_fd]) {
        r->eax = (u32)-1; return;
    }
    if (old_fd == new_fd) { r->eax = new_fd; return; }
    /* Close new_fd if open */
    if (fd_buf[new_fd]) { kfree(fd_buf[new_fd]); }
    fd_buf[new_fd] = fd_buf[old_fd];
    fd_size[new_fd] = fd_size[old_fd];
    fd_pos[new_fd] = fd_pos[old_fd];
    fd_type[new_fd] = fd_type[old_fd];
    r->eax = new_fd;
}

/* Pipe: read_fd (ebx) and write_fd (ecx) returned via user-provided pointers */
static void sys_pipe(registers_t *r) {
    u32 *fds = (u32 *)r->ebx;  /* int fds[2] */
    if (!fds) { r->eax = (u32)-1; return; }

    /* Allocate shared pipe ring buffer */
    pipe_t *pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!pipe) { r->eax = (u32)-1; return; }
    pipe->rpos = 0;
    pipe->wpos = 0;
    pipe->count = 0;
    pipe->refs = 2;  /* read-end + write-end */

    /* Find two free FDs */
    int rfd = -1, wfd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_buf[i]) {
            if (rfd < 0) rfd = i;
            else if (wfd < 0) { wfd = i; break; }
        }
    }
    if (wfd < 0) { kfree(pipe); r->eax = (u32)-1; return; }

    /* Both FDs point to the same pipe struct.
       read-end uses type=2, write-end uses type=3 */
    fd_buf[rfd]   = (u8 *)pipe;
    fd_size[rfd]  = PIPE_BUF_SZ;
    fd_pos[rfd]   = 0;
    fd_type[rfd]  = 2;
    fd_buf[wfd]   = (u8 *)pipe;
    fd_size[wfd]  = PIPE_BUF_SZ;
    fd_pos[wfd]   = 0;
    fd_type[wfd]  = 3;

    fds[0] = rfd;
    fds[1] = wfd;
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
    if (g_output_fd >= 0 && g_output_fd < MAX_FD && fd_buf[g_output_fd]) {
        /* Redirect to file: append one byte */
        if (fd_pos[g_output_fd] < fd_size[g_output_fd]) {
            fd_buf[g_output_fd][fd_pos[g_output_fd]++] = (u8)(r->ebx);
        }
    } else {
        gfx.putc((char)r->ebx);
    }
    r->eax = 0;
}

static void sys_gfx_puts(registers_t *r) {
    if (g_output_fd >= 0 && g_output_fd < MAX_FD && fd_buf[g_output_fd]) {
        /* Redirect to file: append string */
        const char *s = (const char *)r->ebx;
        u8 *buf = fd_buf[g_output_fd];
        u32 size = fd_size[g_output_fd];
        u32 *pos = &fd_pos[g_output_fd];
        while (*s && *pos < size)
            buf[(*pos)++] = (u8)(*s++);
    } else {
        gfx.puts_utf8((const char *)r->ebx);
    }
    r->eax = 0;
}

static void sys_gfx_set_fg(registers_t *r) {
    gfx.set_fg((u8)r->ebx);
    r->eax = 0;
}

static void sys_set_outfd(registers_t *r) {
    int fd = (int)r->ebx;
    /* -1 = restore screen, >=0 = redirect to FD */
    if (fd < -1) fd = -1;
    if (fd >= MAX_FD) fd = -1;
    if (fd >= 0 && !fd_buf[fd]) fd = -1;
    r->eax = g_output_fd;  /* return previous */
    g_output_fd = fd;
}

static void sys_fsync(registers_t *r) {
    u32 fd = r->ebx;
    const char *name = (const char *)r->ecx;
    if (fd >= MAX_FD || !fd_buf[fd] || !name || fd_type[fd] != 1) {
        r->eax = (u32)-1; return;
    }
    /* Write FD buffer to disk file */
    int result = fat_write_file(name, fd_buf[fd], (int)fd_pos[fd]);
    r->eax = (u32)result;
}

/* ---- ramdisk syscalls ---- */

static void sys_rd_create(registers_t *r) {
    const char *name = (const char *)r->ebx;
    const u8 *data = (const u8 *)r->ecx;
    u32 size = r->edx;
    r->eax = (u32)rd_create(name, data, size);
}

static void sys_rd_read(registers_t *r) {
    const char *name = (const char *)r->ebx;
    u8 *out = (u8 *)r->ecx;
    u32 max = r->edx;
    r->eax = (u32)rd_read(name, out, max);
}

static void sys_rd_list(registers_t *r) {
    char *out = (char *)r->ebx;
    u32 max = r->ecx;
    r->eax = (u32)rd_list(out, max);
}

static void sys_rd_remove(registers_t *r) {
    const char *name = (const char *)r->ebx;
    r->eax = (u32)rd_remove(name);
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
    case SYS_GETPID:
        if (current_task)
            r->eax = current_task->pid;
        else
            r->eax = (u32)-1;
        break;
    case SYS_KILL:
        /* Send signal to target PID. SIGKILL is immediate, others pend. */
        r->eax = task_send_signal((u32)r->ebx, (int)r->ecx);
        break;
    case SYS_SIGACTION: {
        /* ebx=signum, ecx=handler (0=SIG_DFL, 1=SIG_IGN, or user addr) */
        int sig = (int)r->ebx;
        u32 handler = r->ecx;
        if (sig < 1 || sig > 31 || !current_task) { r->eax = (u32)-1; break; }
        u32 old = current_task->sig_handlers[sig];
        current_task->sig_handlers[sig] = handler;
        r->eax = old;
        break;
    }
    case SYS_SIGRETURN:
        /* Restore user context saved before signal handler was called */
        if (current_task) {
            r->eip = current_task->sig_saved_eip;
            r->_esp = current_task->sig_saved_esp;
            r->eax = 0;
        }
        break;
    case SYS_STAT:
        sys_stat(r);
        break;
    case SYS_LSEEK:
        sys_lseek(r);
        break;
    case SYS_DUP:
        sys_dup(r);
        break;
    case SYS_DUP2:
        sys_dup2(r);
        break;
    case SYS_PIPE:
        sys_pipe(r);
        break;
    case SYS_FWRITE:
        sys_fwrite(r);
        break;
    case SYS_SET_OUTFD:
        sys_set_outfd(r);
        break;
    case SYS_FSYNC:
        sys_fsync(r);
        break;
    case SYS_RD_CREATE:
        sys_rd_create(r);
        break;
    case SYS_RD_READ:
        sys_rd_read(r);
        break;
    case SYS_RD_LIST:
        sys_rd_list(r);
        break;
    case SYS_RD_REMOVE:
        sys_rd_remove(r);
        break;
    default:
        r->eax = (u32)-1;
        break;
    }
}
