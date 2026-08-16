#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/types.h"
#include "drivers/serial.h"

static PagingManager *kernel_paging = nullptr;

PagingManager::PagingManager() {
    /* Allocate a 4KB physical page for the page directory */
    page_dir_phys_ = mm_alloc_page();
    page_dir_virt_ = (u32 *)page_dir_phys_;

    /* Zero the page directory */
    for (int i = 0; i < 1024; i++)
        page_dir_virt_[i] = 0;

    /* Clone kernel 4MB identity mappings so ISR code + iret
       can execute with this page directory loaded.  PDEs are
       PAGE_PRESENT|PAGE_RW|PAGE_PSE — no PAGE_USER, so ring 3
       cannot touch kernel pages. */
    if (kernel_paging && this != kernel_paging) {
        for (int i = 0; i < 1024; i++) {
            u32 kpde = kernel_paging->page_dir_virt_[i];
            if (kpde & PAGE_PRESENT)
                page_dir_virt_[i] = kpde;
        }
    }
}

PagingManager::~PagingManager() {
    /* 释放登记的 owned 数据页 (sbrk/fork 拷贝页) */
    for (int i = 0; i < owned_count_; i++)
        mm_free_page(owned_phys_[i]);
    owned_count_ = 0;

    /* Free user page tables (PDE indices 0..767 for < 0xC0000000) */
    for (int i = 0; i < 768; i++) {
        u32 pde = page_dir_virt_[i];
        if (pde & PAGE_PRESENT) {
            /* If it's a page table (not a 4MB page), free it */
            if (!(pde & PAGE_PSE))
                mm_free_page(pde & 0xFFFFF000);
        }
    }
    mm_free_page(page_dir_phys_);
}

u32 PagingManager::translate_user(u32 va) const {
    u32 pde = page_dir_virt_[va >> 22];
    if (!(pde & PAGE_PRESENT)) return 0;

    if (pde & PAGE_PSE) {
        /* 4MB 大页 — 必须带 USER 位, 否则是内核 PSE 恒等映射,
           拒绝用户指针指向内核区 */
        if (!(pde & PAGE_USER)) return 0;
        return (pde & 0xFFC00000) + (va & 0x3FFFFF);
    }

    u32 *pt = (u32 *)(pde & 0xFFFFF000);
    u32 pte = pt[(va >> 12) & 0x3FF];
    if (!(pte & PAGE_PRESENT)) return 0;
    if (!(pte & PAGE_USER))    return 0;
    return (pte & 0xFFFFF000) + (va & 0xFFF);
}

void PagingManager::track_owned(u32 phys) {
    /* 溢出时放弃跟踪 (页泄漏但不崩溃) — MAX_OWNED=512 远超
       当前 fork 拷贝(32页)+sbrk 的实际用量 */
    if (owned_count_ < MAX_OWNED)
        owned_phys_[owned_count_++] = phys;
}

void PagingManager::map_page(u32 virt, u32 phys, u32 flags) {
    u32 pde_idx = virt >> 22;
    u32 pte_idx = (virt >> 12) & 0x3FF;

    u32 pde = page_dir_virt_[pde_idx];

    /* If PDE is not present or is a 4MB PSE page, allocate a page table */
    if (!(pde & PAGE_PRESENT) || (pde & PAGE_PSE)) {
        u32 pt_phys = mm_alloc_page();
        if (!pt_phys) return;  /* OOM: 保持 PDE 缺失, 后续访问触发可处理的 #PF */
        u32 *pt_virt = (u32 *)pt_phys;

        /* Zero the page table */
        for (int i = 0; i < 1024; i++)
            pt_virt[i] = 0;

        /* Create PDE pointing to the page table */
        page_dir_virt_[pde_idx] = pt_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        pde = page_dir_virt_[pde_idx];
    }

    /* Get page table virtual address */
    u32 *pt_virt = (u32 *)(pde & 0xFFFFF000);

    /* Set PTE */
    pt_virt[pte_idx] = (phys & 0xFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;
}

void PagingManager::load() {
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_dir_phys_));
}

void PagingManager::map_kernel_4mb(u32 phys_addr) {
    u32 pde_idx = phys_addr >> 22;
    page_dir_virt_[pde_idx] = (phys_addr & 0xFFC00000)
        | PAGE_PRESENT | PAGE_RW | PAGE_PSE;  /* no PAGE_USER — kernel-only */
}

void PagingManager::map_user_4mb(u32 virt_addr, u32 phys_addr) {
    u32 pde_idx = virt_addr >> 22;
    page_dir_virt_[pde_idx] = (phys_addr & 0xFFC00000)
        | PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_PSE;
}

PagingManager *PagingManager::get_kernel_paging() {
    return kernel_paging;
}

void PagingManager::init_kernel_paging() {
    kernel_paging = new PagingManager();

    /* Identity-map first 64MB as 4MB PSE pages */
    for (int i = 0; i < 16; i++)
        kernel_paging->map_kernel_4mb(i * 0x400000);

    /* Also map framebuffer region */
    u32 fbaddr = *(u32 *)0x500;
    if (fbaddr >= 0x100000) {
        kernel_paging->map_kernel_4mb(fbaddr);
    }

    /* Ensure PSE is enabled (GRUB may have already done this) */
    u32 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    /* 开启分页 (CR0.PG)! stage2 只设了 PE (or al,1), PG 从未开启 —
       PG=0 时 mov cr3 全被硬件忽略, 整个系统实际跑在纯段式恒等模式:
       用户页表/fork 深拷贝/exec 私有页从未生效, 仅靠"所有关键区域
       恰好恒等"侥幸工作 (实测: exec 换非恒等私有页后 ring3 仍执行
       旧恒等镜像, CR0=0x11 PG=0 铁证)。
       此刻 CPU 在低地址执行, 0-64MB PSE 恒等覆盖切换点 — 安全。 */
    kernel_paging->load();
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;   /* PG */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

void paging_init() {
    PagingManager::init_kernel_paging();
}
