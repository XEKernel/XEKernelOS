#include "drivers/input.h"

/* 256 项环形缓冲。鼠标 100 包/s + 键盘突发, 消费端 (GUI) 10ms 轮询
   足够; 满时丢最旧事件 (新事件更有时效性) */
#define IN_RING 256

static input_event g_ring[IN_RING];
static volatile u32 g_head = 0, g_tail = 0;

void input_push(u32 type, i32 x, i32 y, u32 arg) {
    u32 next = (g_head + 1) % IN_RING;
    if (next == g_tail)                    /* 满: 覆盖最旧 */
        g_tail = (g_tail + 1) % IN_RING;
    g_ring[g_head].type = type;
    g_ring[g_head].x    = x;
    g_ring[g_head].y    = y;
    g_ring[g_head].arg  = arg;
    g_head = next;
}

int input_pop(input_event *out) {
    if (g_tail == g_head) return 0;
    out->type = g_ring[g_tail].type;
    out->x    = g_ring[g_tail].x;
    out->y    = g_ring[g_tail].y;
    out->arg  = g_ring[g_tail].arg;
    g_tail = (g_tail + 1) % IN_RING;
    return (int)sizeof(input_event);
}
