#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "drivers/serial.h"

u32 ElfLoader::load(const char *path, PagingManager *paging) {
    /* Read entire ELF into a buffer (max 64KB for now) */
    u8 *elf_buf = (u8 *)kmalloc(65536);
    if (!elf_buf) {
        serial_write_str("elf: out of memory\n");
        return 0;
    }

    int sz = fat_read_file_buf(path, elf_buf, 65536);
    if (sz <= 0) {
        serial_write_str("elf: failed to read file\n");
        kfree(elf_buf);
        return 0;
    }

    if (sz < (int)sizeof(Elf32_Ehdr)) {
        serial_write_str("elf: file too small\n");
        kfree(elf_buf);
        return 0;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_buf;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        serial_write_str("elf: bad magic\n");
        kfree(elf_buf);
        return 0;
    }

    if (ehdr->e_type != 2) {
        serial_write_str("elf: not executable\n");
        kfree(elf_buf);
        return 0;
    }
    if (ehdr->e_machine != 3) {
        serial_write_str("elf: not i386\n");
        kfree(elf_buf);
        return 0;
    }

    u32 entry = ehdr->e_entry;
    serial_write_str("elf: entry=0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_write_char(hex[(entry >> i) & 15]);
    serial_write_char('\n');

    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        serial_write_str("elf: no program headers\n");
        kfree(elf_buf);
        return 0;
    }

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(elf_buf + ehdr->e_phoff);

    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        /* Only load executable segments — skip PHDR/note/etc segments
           that ld.lld places at the link base address (e.g., 0x400000) */
        if (!(ph->p_flags & 1)) continue;  /* PF_X */

        u32 vaddr_start = ph->p_vaddr;
        u32 vaddr_end   = ph->p_vaddr + ph->p_memsz;
        u32 page_start  = vaddr_start & 0xFFFFF000;
        u32 page_end    = (vaddr_end + 0xFFF) & 0xFFFFF000;

        serial_write_str("elf: segment vaddr=0x");
        for (int j = 28; j >= 0; j -= 4)
            serial_write_char(hex[(vaddr_start >> j) & 15]);
        serial_write_str("\n");

        /* Allocate + copy page by page via physical address,
           then map. This avoids the kernel-page-table write trap:
           writing to user vaddr through kernel CR3 hits the
           kernel's identity mapping (wrong phys page), not the
           user's allocated page. */
        for (u32 page = page_start; page < page_end; page += 0x1000) {
            u32 phys = mm_alloc_page();
            if (!phys) {
                serial_write_str("elf: out of physical memory\n");
                kfree(elf_buf);
                return 0;
            }

            /* Copy file data to physical page (identity-mapped, <16MB) */
            if (ph->p_filesz > 0) {
                u32 seg_off = page - vaddr_start;
                u32 copy_start = ph->p_offset + seg_off;
                if (copy_start < (u32)sz) {
                    u32 copy_len = ph->p_filesz - seg_off;
                    if (copy_len > 0x1000) copy_len = 0x1000;
                    u8 *dst = (u8 *)phys;
                    for (u32 k = 0; k < copy_len; k++)
                        dst[k] = elf_buf[copy_start + k];
                }
            }

            /* Zero BSS portion within this page */
            u32 page_end_va = page + 0x1000;
            if (page_end_va > vaddr_start + ph->p_filesz) {
                u32 bss_start = (vaddr_start + ph->p_filesz > page)
                    ? (vaddr_start + ph->p_filesz - page) : 0;
                u32 bss_end = (page_end_va < vaddr_end)
                    ? 0x1000 : (vaddr_end - page);
                u8 *phys_ptr = (u8 *)(phys + bss_start);
                for (u32 k = bss_start; k < bss_end; k++)
                    ((u8 *)phys)[k] = 0;
            }

            /* Now map the physical page into user address space */
            paging->map_page(page, phys, PT_FLAGS);
        }
    }

    kfree(elf_buf);
    return entry;
}
