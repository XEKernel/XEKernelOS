#include "kernel/mm.h"

#define PAGE_SIZE   4096
#define MEM_TOP     0x4000000   /* 64 MB */

static u8  *bitmap;
static u32  total_pages;
static u32  free_count;
static u32  first_page;

extern u32 _bss_end[];
extern u8  _heap_start[], _heap_end[];   /* linker.ld 定义的内核堆 */

void mm_reserve(u32 start, u32 end) {
    if (end > MEM_TOP) end = MEM_TOP;
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

    total_pages = (MEM_TOP - bm_start) / PAGE_SIZE;
    u32 bm_bytes = (total_pages + 7) / 8;
    u32 bm_pages = (bm_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    bitmap    = (u8 *)bm_start;
    first_page = bm_start + bm_pages * PAGE_SIZE;
    free_count = (MEM_TOP - first_page) / PAGE_SIZE;

    for (u32 i = 0; i < bm_bytes; i++) bitmap[i] = 0;
    for (u32 i = 0; i < bm_pages; i++) {
        u32 byte = i / 8;
        u32 bit  = i % 8;
        bitmap[byte] |= (1 << bit);
    }

    /* 预留不由 mm_alloc_page 分配、但被长期占用的物理区:
       1) 内核堆 (kmalloc, linker.ld: 0x1000000~0x1F00000)
       2) 用户程序恒等映射区 (loader/exec: 代码 0x400000 + 栈 64KB,
          预留到 0x430000 留余量) — 不预留会被页表/内核栈分配抢占
       3) VBE 显存 (若位于 64MB 物理范围内) */
    mm_reserve((u32)_heap_start, (u32)_heap_end);
    mm_reserve(0x400000, 0x430000);
    u32 fbaddr = *(u32 *)0x500;
    if (fbaddr >= 0x100000 && fbaddr < MEM_TOP)
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
