#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
#include "drivers/serial.h"

#define ELF_LOAD_PHYS 0x01000000  /* physical 16MB, 4MB-aligned for PSE */

u32 ElfLoader::load(const char *path, PagingManager *paging) {
    u8 *elf_buf = (u8 *)kmalloc(65536);
    if (!elf_buf) { serial_write_str("elf: out of memory\n"); return 0; }

    int sz = fat_read_file_buf(path, elf_buf, 65536);
    if (sz <= 0) { kfree(elf_buf); return 0; }

    if (sz < (int)sizeof(Elf32_Ehdr)) { kfree(elf_buf); return 0; }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_buf;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F')
        { kfree(elf_buf); return 0; }
    if (ehdr->e_type != 2 || ehdr->e_machine != 3) { kfree(elf_buf); return 0; }

    u32 entry = ehdr->e_entry;
    serial_write_str("elf: entry=0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_write_char(hex[(entry >> i) & 15]);
    serial_write_str("\n");

    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) { kfree(elf_buf); return 0; }

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(elf_buf + ehdr->e_phoff);

    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (!(ph->p_flags & 1)) continue;

        u32 vaddr_start = ph->p_vaddr;
        serial_write_str("elf: segment vaddr=0x");
        for (int j = 28; j >= 0; j -= 4)
            serial_write_char(hex[(vaddr_start >> j) & 15]);
        serial_write_str("\n");

        /* Copy to physical address: ELF_LOAD_PHYS + (vaddr - entry region).
           All segments must be within the same 4MB PSE page. */
        u32 phys_base = ELF_LOAD_PHYS + (vaddr_start - entry);
        u8 *phys_dst = (u8 *)phys_base;

        /* Copy file data */
        if (ph->p_filesz > 0 && ph->p_offset + ph->p_filesz <= (u32)sz) {
            for (u32 k = 0; k < ph->p_filesz; k++)
                phys_dst[k] = elf_buf[ph->p_offset + k];
        }

        /* Zero BSS */
        if (ph->p_memsz > ph->p_filesz) {
            for (u32 k = ph->p_filesz; k < ph->p_memsz; k++)
                phys_dst[k] = 0;
        }
    }

    kfree(elf_buf);

    /* Map entire 4MB region as a user PSE page */
    paging->map_user_4mb(entry & 0xFFC00000, ELF_LOAD_PHYS);

    return entry;
}
