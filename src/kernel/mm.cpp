#include "kernel/mm.h"
#include "drivers/serial.h"

#define PAGE_SIZE   4096
#define MEM_TOP_MAX 0x4000000   /* 内核恒等映射上限 64MB */

static u8  *bitmap;
static u32  total_pages;
static u32  free_count;
static u32  first_page;
static u32  mem_top = MEM_TOP_MAX;

extern u32 _bss_end[];
extern u8  _heap_start[], _heap_end[];   /* linker.ld 定义的内核堆 */

void mm_reserve(u32 start, u32 end) {
    if (end > mem_top) end = mem_top;
    u32 s = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u32 e = end & ~(PAGE_SIZE - 1);
    for (u32 addr = s; addr < e; addr += PAGE_SIZE) {
        if (addr < first_page) continue;
        u32 idx = (addr - first_page) / PAGE_SIZE;
        u32 byte = idx / 8;
        u32 bit  = idx % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (1 << bit);
            free_count--;
        }
    }
}

void mm_init(void) {
    u32 kern_end = (u32)_bss_end;
    u32 bm_start = (kern_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* 物理内存上界: stage2 探测后写入 0x514 (单位: 字节, 0 = 探测失败)。
       旧实现硬编码 64MB — 在 -m 32 的 QEMU 或 32MB 的 VMware 虚拟机上,
       mm 会把超出实际 RAM 的"不存在的物理页"分配出去, 只在内存吃紧时
       暴露为随机崩溃。 */
    u32 det = *(u32 *)0x514;
    if (det == 0) {
        serial_write_str("mm: RAM probe failed, assume 64MB\n");
        mem_top = MEM_TOP_MAX;
    } else {
        mem_top = det;
        if (mem_top > MEM_TOP_MAX) mem_top = MEM_TOP_MAX;
    }
    serial_write_str("mm: detected RAM ");
    serial_write_u32(mem_top >> 20);
    serial_write_str("MB\n");

    /* 内核堆固定在 0x1000000~0x1F00000 (linker.ld), 物理内存必须覆盖它
       — 否则 kmalloc 会写进不存在的物理页。必须先明确报错停机。 */
    if (mem_top < 0x1F00000) {
        serial_write_str("mm: FATAL - RAM below kernel heap end (need >= 31MB)\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    total_pages = (mem_top - bm_start) / PAGE_SIZE;
    u32 bm_bytes = (total_pages + 7) / 8;
    u32 bm_pages = (bm_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    bitmap    = (u8 *)bm_start;
    first_page = bm_start + bm_pages * PAGE_SIZE;

    /* 位图自身占用的 bm_pages 页必须从"可分配页数"里扣除。
       旧实现 total_pages 从 bm_start 起算, 而 mm_alloc_page 返回的地址
       从 first_page 起算 → 尾部 bm_pages 次分配会返回内存上界之外的
       物理地址 (mem_top 之前已被 mm_reserve 之外的区域), 属于越界分配。
       实际未触发只是因为可分配页远多于用量, 属潜在缺陷。 */
    total_pages = (total_pages > bm_pages) ? (total_pages - bm_pages) : 0;
    free_count  = total_pages;

    for (u32 i = 0; i < bm_bytes; i++) bitmap[i] = 0;
    for (u32 i = 0; i < bm_pages; i++) {
        u32 byte = i / 8;
        u32 bit  = i % 8;
        bitmap[byte] |= (1 << bit);
    }

    /* 预留不由 mm_alloc_page 分配、但被长期占用的物理区:
       1) 内核堆 (kmalloc, linker.ld: 0x1000000~0x1F00000)
       2) 用户程序恒等映射区 (loader/shell_launch_user: 代码 0x400000
          + .bss 预留 + 栈 0x430000~0x440000) — 预留到 0x450000 留余量,
          不预留会被页表/内核栈分配抢占
       3) VBE 显存 (若位于物理内存范围内; VMware 的线性帧缓冲常在
          0xE0000000 附近, 超出 mem_top → 不在 mm 管理范围, 无需预留) */
    mm_reserve((u32)_heap_start, (u32)_heap_end);
    mm_reserve(0x400000, 0x450000);
    u32 fbaddr = *(u32 *)0x500;
    if (fbaddr >= 0x100000 && fbaddr < mem_top)
        mm_reserve(fbaddr & ~0x3FFFFF, (fbaddr & ~0x3FFFFF) + 0x400000);
}

u32 mm_alloc_page(void) {
    for (u32 i = 0; i < total_pages; i++) {
        u32 byte = i / 8;
        u32 bit  = i % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (1 << bit);
            free_count--;
            return first_page + i * PAGE_SIZE;
        }
    }
    return 0;
}

void mm_free_page(u32 addr) {
    if (addr < first_page) return;
    u32 idx = (addr - first_page) / PAGE_SIZE;
    if (idx >= total_pages) return;
    u32 byte = idx / 8;
    u32 bit  = idx % 8;
    if (bitmap[byte] & (1 << bit)) {
        bitmap[byte] &= ~(1 << bit);
        free_count++;
    }
}

u32 mm_free_count(void) {
    return free_count;
}
u32 mm_total_pages(void) {
    return total_pages;
}
