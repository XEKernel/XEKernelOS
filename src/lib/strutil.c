#include "lib/strutil.h"
#include "lib/types.h"

int strcmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return (u8)ca - (u8)cb;
        a++; b++;
    }
    return (u8)*a - (u8)*b;
}

int strncmp_ci(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = (a[i]>='a'&&a[i]<='z') ? a[i]-32 : a[i];
        char cb = (b[i]>='a'&&b[i]<='z') ? b[i]-32 : b[i];
        if (ca != cb) return (u8)ca - (u8)cb;
        if (!a[i] || !b[i]) break;
    }
    return 0;
}
