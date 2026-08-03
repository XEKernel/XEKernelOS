#pragma once
#include "lib/types.h"

/* ELF32 header */
struct Elf32_Ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

/* ELF32 program header */
struct Elf32_Phdr {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
};

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

#define ELF_MAGIC   0x464C457F  /* "\x7FELF" in little-endian */

class PagingManager;

class ElfLoader {
public:
    /* Load ELF from FAT12 path, map segments using paging.
       Returns entry point (0 on failure). */
    static u32 load(const char *path, PagingManager *paging);
};
