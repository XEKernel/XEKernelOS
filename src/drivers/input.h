#pragma once
#include "lib/types.h"

/* 统一输入事件 (准则一: input 也是 fd, /dev/input 读取)
   type: 1=鼠标移动  2=鼠标按键变化  3=键盘扫描码 */
struct input_event {
    u32 type;
    i32 x, y;    /* 鼠标事件: 坐标; 键盘事件: 0 */
    u32 arg;     /* 鼠标: 按键位 (bit0=左 bit1=右 bit2=中); 键盘: 扫描码 */
};

void input_push(u32 type, i32 x, i32 y, u32 arg);
int  input_pop(input_event *out);   /* 返回 16 或 0(空) */
