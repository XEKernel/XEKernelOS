#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/mm.h"
#include "lib/heap.h"
#include "fs/fat12.h"
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

    /* Validate ELF header */
    if (sz < (int)sizeof(Elf32_Ehdr)) {
        serial_write_str("elf: file too small\n");
        kfree(elf_buf);
        return 0;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_buf;

    /* Check magic: 0x7F 'E' 'L' 'F' */
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        serial_write_str("elf: bad magic\n");
        kfree(elf_buf);
        return 0;
    }

    /* Must be 32-bit, little-endian, executable */
    if (ehdr->e_type != 2) {  /* ET_EXEC */
        serial_write_str("elf: not executable\n");
        kfree(elf_buf);
        return 0;
    }
    if (ehdr->e_machine != 3) {  /* EM_386 */
        serial_write_str("elf: not i386\n");
        kfree(elf_buf);
        return 0;
    }

    u32 entry = ehdr->e_entry;

    serial_write_str("elf: entry=0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_write_char(hex[(entry >> i) & 15]);
    serial_write_str("\n");

    /* Parse program headers */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        serial_write_str("elf: no program headers\n");
        kfree(elf_buf);
        return 0;
    }

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(elf_buf + ehdr->e_phoff);

    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) continue;

        u32 vaddr_start = ph->p_vaddr;
        u32 vaddr_end   = ph->p_vaddr + ph->p_memsz;
        u32 page_start  = vaddr_start & 0xFFFFF000;
        u32 page_end    = (vaddr_end + 0xFFF) & 0xFFFFF000;

        serial_write_str("elf: segment vaddr=0x");
        for (int j = 28; j >= 0; j -= 4)
            serial_write_char(hex[(vaddr_start >> j) & 15]);
        serial_write_str(" size=");
        serial_write_char('0' + (ph->p_memsz / 100000) % 10);
        serial_write_char('0' + (ph->p_memsz / 10000) % 10);
        serial_write_char('0' + (ph->p_memsz / 1000) % 10);
        serial_write_char('0' + (ph->p_memsz / 100) % 10);
        serial_write_char('0' + (ph->p_memsz / 10) % 10);
        serial_write_char('0' + ph->p_memsz % 10);
        serial_write_str("\n");

        /* Map pages */
        for (u32 page = page_start; page < page_end; page += 0x1000) {
            u32 phys = mm_alloc_page();
            if (!phys) {
                serial_write_str("elf: out of physical memory\n");
                kfree(elf_buf);
                return 0;
            }
            paging->map_page(page, phys, PT_FLAGS);
        }

        /* Copy file data to virtual addresses */
        if (ph->p_filesz > 0) {
            u8 *src = elf_buf + ph->p_offset;
            u8 *dst = (u8 *)ph->p_vaddr;
            for (u32 j = 0; j < ph->p_filesz; j++)
                dst[j] = src[j];
        }

        /* Zero BSS: memsz - filesz */
        if (ph->p_memsz > ph->p_filesz) {
            u8 *bss = (u8 *)(ph->p_vaddr + ph->p_filesz);
            u32 bss_len = ph->p_memsz - ph->p_filesz;
            for (u32 j = 0; j < bss_len; j++)
                bss[j] = 0;
        }
    }

    kfree(elf_buf);
    return entry;
}
