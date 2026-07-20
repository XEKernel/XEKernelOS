/* Minimal C++ runtime support for kernel */
#include "lib/types.h"

/* operator new — uses kmalloc; for early-init use static allocation */
void *operator new(unsigned int size) {
    extern void *kmalloc(u32 sz);
    return kmalloc((u32)size);
}

void operator delete(void *p) noexcept {
    extern void kfree(void *p);
    if (p) kfree(p);
}

void operator delete(void *p, unsigned int) noexcept {
    extern void kfree(void *p);
    if (p) kfree(p);
}

/* Placement new/delete */
void *operator new(unsigned int, void *p) noexcept { return p; }
void operator delete(void *, void *) noexcept {}

/* Pure virtual function stub (if anyone uses virtual) */
extern "C" void __cxa_pure_virtual() {
    for (;;) __asm__ volatile("hlt");
}

/* Called before/after static constructors; we have none */
extern "C" void __cxa_atexit() {}
