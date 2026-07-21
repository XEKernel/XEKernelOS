// Chinese font — loaded from disk at runtime (font_cn.bin on FAT12)
// Format: uint16 count LE | { uint16 codepoint LE | uint8 bitmap[32] }...
#pragma once

#include "lib/types.h"

struct __attribute__((packed)) GlyphEntry {
    unsigned short cp;
    unsigned char  bitmap[32];
};

extern int  font_cn_loaded;       // 0 = not loaded, 1 = loaded
extern u8  *font_cn_buf;          // raw buffer (malloc'd)
extern int  font_cn_count_val;    // cached count

/* Load font_cn.bin from FAT12 filesystem. Must be called after fat_init. */
void font_cn_load(void);

/* Binary search for a glyph. Returns bitmap pointer or nullptr. */
const unsigned char *font_cn_lookup_impl(u16 cp);
