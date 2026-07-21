#pragma once
#include "lib/types.h"

class Mouse {
public:
    void init();
    int  get(int *x, int *y, int *btn);
    void feed_byte(u8 data);   /* external byte feed (keyboard polling loop) */

private:
    int  mx_ = 0, my_ = 0, mbtn_ = 0;
    int  cycle_ = 0;
    u8   pkt_[3]{};

    static void wait(u8 write_mode);
    static void write(u8 data);
    static u8   read();
    static void irq_handler();
};

extern Mouse mouse;

inline void mouse_init()                { mouse.init(); }
inline int  mouse_get(int *x, int *y, int *b) { return mouse.get(x, y, b); }
