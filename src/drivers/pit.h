#pragma once
#include "lib/types.h"

class PitTimer {
public:
    void init();
    void sleep(int ms);
    u32  ticks() const { return ticks_; }

private:
    static volatile u32 ticks_;
    static void handler();
};

extern PitTimer pit;

inline void pit_init()      { pit.init(); }
inline void pit_sleep(int ms) { pit.sleep(ms); }
