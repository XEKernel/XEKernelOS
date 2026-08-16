#include "lib/uaccess.h"
#include "kernel/paging.h"
#include "kernel/task.h"
#include "kernel/user.h"

static PagingManager *current_user_pd(void) {
    if (current_task && current_task->paging)
        return current_task->paging;
    return g_user_pd;   /* enter_user_mode 之前的裸用户态路径 */
}

bool copy_from_user(void *kdst, u32 usrc, u32 len) {
    u8 *d = (u8 *)kdst;
    PagingManager *pd = current_user_pd();
    if (!pd || !d) return false;

    while (len) {
        u32 phys = pd->translate_user(usrc);
        if (!phys) return false;
        u32 off = usrc & 0xFFF;
        u32 n = 0x1000 - off;
        if (n > len) n = len;
        const u8 *s = (const u8 *)phys;   /* 物理地址经内核 PSE 恒等可达 */
        for (u32 i = 0; i < n; i++) d[i] = s[i];
        d += n; usrc += n; len -= n;
    }
    return true;
}

bool copy_to_user(u32 udst, const void *ksrc, u32 len) {
    const u8 *s = (const u8 *)ksrc;
    PagingManager *pd = current_user_pd();
    if (!pd || !s) return false;

    while (len) {
        u32 phys = pd->translate_user(udst);
        if (!phys) return false;
        u32 off = udst & 0xFFF;
        u32 n = 0x1000 - off;
        if (n > len) n = len;
        u8 *d = (u8 *)phys;
        for (u32 i = 0; i < n; i++) d[i] = s[i];
        s += n; udst += n; len -= n;
    }
    /* 同一 CPU 上 PSE 与 4KB 映射指向相同物理缓存行, 数据天然一致,
       无需 wbinvd/invlpg (旧代码的缓存别名担忧对 WB 内存不成立) */
    return true;
}

bool copy_str_from_user(char *kdst, u32 usrc, u32 max) {
    if (max == 0 || !kdst) return false;
    kdst[0] = 0;
    PagingManager *pd = current_user_pd();
    if (!pd) return false;

    u32 done = 0;
    while (done < max - 1) {
        u32 va = usrc + done;
        u32 phys = pd->translate_user(va);
        if (!phys) return done > 0;   /* 首字节即未映射 → false */
        const u8 *s = (const u8 *)phys;
        u32 off = va & 0xFFF;
        u32 n = 0x1000 - off;
        if (n > max - 1 - done) n = max - 1 - done;
        for (u32 i = 0; i < n; i++) {
            kdst[done++] = (char)s[i];
            if (s[i] == 0) return true;   /* 含 NUL 拷贝完成 */
        }
    }
    kdst[done] = 0;
    return true;   /* 达 max-1 截断, 视为成功 */
}
