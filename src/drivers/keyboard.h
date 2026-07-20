#pragma once
#include "lib/types.h"
#include "lib/ports.h"

#define KB_DATA   0x60
#define KB_STATUS 0x64
#define CMD_BUF   64

class Keyboard {
public:
    void init();
    char getchar();
    void readline(char *b, int max);
    void flush();
    int  ctrl_c();

private:
    static const char kbd_low_[];
    static const char kbd_up_[];
    static constexpr u8 SC_LSHIFT = 0x2A;
    static constexpr u8 SC_RSHIFT = 0x36;
    static constexpr u8 SC_CAPS   = 0x3A;

    int  shift_ = 0;
    int  caps_  = 0;
    int  ctrl_  = 0;

    void cmd(u8 cmd);
    u8   await();
    u8   read_scan();
};

extern Keyboard kb;

inline void kb_init()            { kb.init(); }
inline char kb_getchar()         { return kb.getchar(); }
inline void kb_readline(char *b, int m) { kb.readline(b, m); }
inline void kb_flush()           { kb.flush(); }
inline int  kb_ctrl_c()          { return kb.ctrl_c(); }
