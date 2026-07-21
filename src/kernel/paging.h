#pragma once
#include "lib/types.h"

#define PAGE_PRESENT  0x01
#define PAGE_RW       0x02
#define PAGE_USER     0x04
#define PAGE_PSE      0x80   /* 4MB page */

/* 4KB page table entry flags */
#define PT_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_USER)

class PagingManager {
public:
    PagingManager();
    ~PagingManager();

    /* Map 4KB page: virt → phys */
    void map_page(u32 virt, u32 phys, u32 flags);

    /* Load this page directory into CR3 (flush TLB) */
    void load();

    /* Get physical address of page directory (for CR3) */
    u32 get_page_dir_phys() const { return page_dir_phys_; }

    /* Get kernel page table singleton */
    static PagingManager *get_kernel_paging();
    static void init_kernel_paging();

    /* Identity-map a 4MB region for kernel space */
    void map_kernel_4mb(u32 phys_addr);

    /* Identity-map a 4MB region for user space (with PAGE_USER) */
    void map_user_4mb(u32 virt_addr, u32 phys_addr);

private:
    u32  page_dir_phys_;   /* physical address of page directory (4KB aligned) */
    u32 *page_dir_virt_;   /* virtual address (identity-mapped) */
};

void paging_init();
