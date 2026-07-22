#include "kernel/mm.h"

#define PAGE_SIZE   4096
#define MEM_TOP     0x4000000   /* 64 MB */

static u8  *bitmap;
static u32  total_pages;
static u32  free_count;
static u32  first_page;

extern u32 _bss_end[];

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
