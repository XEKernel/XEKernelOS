#include "lib/heap.h"

#define HEAP_MAGIC 0xDEADBEEF
#define HEADER_SIZE ((u32)sizeof(header_t))

typedef struct header {
    u32          magic;
    u32          size;
    struct header *next;
    struct header *prev;
    u8           used;
} header_t;

extern u8 _heap_start[];
extern u8 _heap_end[];

static header_t *heap_base;

void heap_init(void) {
    heap_base = (header_t *)_heap_start;
    heap_base->magic = HEAP_MAGIC;
    heap_base->size  = (u32)_heap_end - (u32)_heap_start;
    heap_base->next  = 0;
    heap_base->prev  = 0;
    heap_base->used  = 0;
}

void *kmalloc(u32 size) {
    if (!size) return 0;
    u32 needed = size + HEADER_SIZE;
    if (needed < HEADER_SIZE + 16) needed = HEADER_SIZE + 16;
    header_t *cur = (header_t *)_heap_start;
    while (cur) {
        if (cur->magic != HEAP_MAGIC) return 0;
        if (!cur->used && cur->size >= needed) {
            if (cur->size >= needed + HEADER_SIZE + 16) {
                header_t *new_h = (header_t *)((u8 *)cur + needed);
                new_h->magic = HEAP_MAGIC;
                new_h->size  = cur->size - needed;
                new_h->next  = cur->next;
                new_h->prev  = cur;
                new_h->used  = 0;
                if (cur->next) cur->next->prev = new_h;
                cur->next = new_h;
                cur->size = needed;
            }
            cur->used = 1;
            return (void *)((u8 *)cur + HEADER_SIZE);
        }
        cur = cur->next;
    }
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    header_t *h = (header_t *)((u8 *)ptr - HEADER_SIZE);
    if (h->magic != HEAP_MAGIC) return;
    h->used = 0;
    if (h->next && !h->next->used && h->next->magic == HEAP_MAGIC) {
        h->size += h->next->size;
        h->next = h->next->next;
        if (h->next) h->next->prev = h;
    }
    if (h->prev && !h->prev->used && h->prev->magic == HEAP_MAGIC) {
        h->prev->size += h->size;
        h->prev->next = h->next;
        if (h->next) h->next->prev = h->prev;
    }
}
