#pragma once
#include "lib/types.h"
#include "lib/ports.h"

class PicController {
public:
    void remap();
    void unmask_irq(int irq);
};

extern PicController pic;

inline void pic_remap()           { pic.remap(); }
inline void pic_unmask_irq(int i) { pic.unmask_irq(i); }
