#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/types.h"

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

void PagingManager::map_page(u32 virt, u32 phys, u32 flags) {
    u32 pde_idx = virt >> 22;
    u32 pte_idx = (virt >> 12) & 0x3FF;

    u32 pde = page_dir_virt_[pde_idx];

    /* If PDE is not present or is a 4MB PSE page, allocate a page table */
    if (!(pde & PAGE_PRESENT) || (pde & PAGE_PSE)) {
        u32 pt_phys = mm_alloc_page();
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
        | PAGE_PRESENT | PAGE_RW | PAGE_PSE;
}

PagingManager *PagingManager::get_kernel_paging() {
    return kernel_paging;
}

void PagingManager::init_kernel_paging() {
    kernel_paging = new PagingManager();

    /* Identity-map first 16MB + heap region as 4MB PSE pages
       PDE 0-3:  kernel code/data (0-16MB)
       PDE 4-7:  heap (0x01000000-0x01FFFFFF from linker.ld) */
    for (int i = 0; i < 8; i++)
        kernel_paging->map_kernel_4mb(i * 0x400000);

    /* Also map framebuffer region (may be above 16MB) */
    u32 fbaddr = *(u32 *)0x500;
    if (fbaddr >= 0x100000) {
        kernel_paging->map_kernel_4mb(fbaddr);
    }

    /* Enable PSE */
    u32 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    /* Load page directory */
    kernel_paging->load();

    /* Enable paging */
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile(
        "mov %0, %%cr0\n"
        "jmp 1f\n"
        "1:\n"
        :
        : "r"(cr0)
    );
}

void paging_init() {
    PagingManager::init_kernel_paging();
}
