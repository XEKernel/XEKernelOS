/* Block cache — LRU cache for ATA disk sectors */

#include "drivers/bcache.h"
#include "drivers/ata.h"
#include "lib/heap.h"
#include "drivers/serial.h"

#define BC_ENTRIES 64

struct bc_entry {
    u32  lba;           /* sector LBA, ~0 = invalid */
    u8   data[512];     /* sector data */
    bool dirty;         /* needs write-back */
};

static bc_entry g_cache[BC_ENTRIES];
static bool     g_init = false;

void bc_init(void) {
    for (int i = 0; i < BC_ENTRIES; i++) {
        g_cache[i].lba = ~0u;
        g_cache[i].dirty = false;
    }
    g_init = true;
}

static bc_entry *bc_lookup(u32 lba) {
    for (int i = 0; i < BC_ENTRIES; i++) {
        if (g_cache[i].lba == lba)
            return &g_cache[i];
    }
    return nullptr;
}

/* Evict the LRU entry (oldest non-pinned).
   Returns pointer to the evicted slot (ready to reuse). */
static bc_entry *bc_evict(void) {
    /* Simple clock-like: pick any non-dirty slot first, then oldest dirty */
    static int hand = 0;

    /* First pass: try to find a clean slot */
    for (int tries = 0; tries < BC_ENTRIES * 2; tries++) {
        hand = (hand + 1) % BC_ENTRIES;
        if (g_cache[hand].lba == ~0u || !g_cache[hand].dirty)
            return &g_cache[hand];
    }

    /* All dirty: write back the current hand entry */
    bc_entry *e = &g_cache[hand];
    if (e->dirty && e->lba != ~0u) {
        ata_write(e->lba, 1, (const u16 *)e->data);
        e->dirty = false;
    }
    hand = (hand + 1) % BC_ENTRIES;
    return e;
}

int bc_read(u32 lba, u8 count, void *buf) {
    if (!g_init) bc_init();
    u8 *dst = (u8 *)buf;

    for (u8 s = 0; s < count; s++) {
        u32 cur_lba = lba + s;
        u8 *sector_dst = dst + s * 512;

        bc_entry *e = bc_lookup(cur_lba);
        if (e) {
            /* Cache hit */
            for (int i = 0; i < 512; i++)
                sector_dst[i] = e->data[i];
        } else {
            /* Cache miss: read from disk into cache */
            e = bc_evict();

            /* Write back if dirty */
            if (e->dirty && e->lba != ~0u) {
                ata_write(e->lba, 1, (const u16 *)e->data);
            }

            /* Read from disk */
            int r = ata_read(cur_lba, 1, (u16 *)e->data);
            if (r < 0) return -1;

            e->lba = cur_lba;
            e->dirty = false;

            /* Copy to caller */
            for (int i = 0; i < 512; i++)
                sector_dst[i] = e->data[i];
        }
    }
    return 0;
}

int bc_write(u32 lba, u8 count, const void *buf) {
    if (!g_init) bc_init();
    const u8 *src = (const u8 *)buf;

    for (u8 s = 0; s < count; s++) {
        u32 cur_lba = lba + s;
        const u8 *sector_src = src + s * 512;

        /* Write-through: immediately write to disk */
        ata_write(cur_lba, 1, (const u16 *)sector_src);

        /* Also update cache if present */
        bc_entry *e = bc_lookup(cur_lba);
        if (!e) {
            e = bc_evict();
            e->lba = cur_lba;
        }
        for (int i = 0; i < 512; i++)
            e->data[i] = sector_src[i];
        e->dirty = false;  /* already written to disk */
    }
    return 0;
}

void bc_flush(void) {
    if (!g_init) return;
    for (int i = 0; i < BC_ENTRIES; i++) {
        if (g_cache[i].dirty && g_cache[i].lba != ~0u) {
            ata_write(g_cache[i].lba, 1, (const u16 *)g_cache[i].data);
            g_cache[i].dirty = false;
        }
    }
}
