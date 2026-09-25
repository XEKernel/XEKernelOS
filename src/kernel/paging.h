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

    /* Read a raw PDE entry (for fork address-space cloning) */
    u32 get_pde(u32 idx) const { return page_dir_virt_[idx]; }

    /* Write a raw PDE entry (for cloning 4MB user PSE mappings in fork) */
    void set_pde(u32 idx, u32 val) { page_dir_virt_[idx] = val; }

    /* 用户虚拟地址 → 物理地址 (要求 USER 权限页)。
       返回 0 表示未映射或非用户页 — syscall 用户指针访问/信号
       handler 校验统一走这里, 不再依赖恒等映射假设。 */
    u32 translate_user(u32 va) const;

    /* 登记本页目录"拥有"的物理数据页 (sbrk/fork 拷贝页)。
       析构时统一释放 — 修复用户数据页永不回收的慢泄漏。
       恒等映射页 (loader 的 0x400000~0x450000) 由 mm
       统一预留, 不在此登记。 */
    void track_owned(u32 phys);

private:
    u32  page_dir_phys_;   /* physical address of page directory (4KB aligned) */
    u32 *page_dir_virt_;   /* virtual address (identity-mapped) */

    static constexpr int MAX_OWNED = 512;
    u32  owned_phys_[MAX_OWNED];
    int  owned_count_ = 0;
};

void paging_init();
