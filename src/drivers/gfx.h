#pragma once
#include "lib/types.h"

/* Color constants */
#define COLOR_BLACK   0x00
#define COLOR_BLUE    0x01
#define COLOR_GREEN   0x02
#define COLOR_CYAN    0x03
#define COLOR_RED     0x04
#define COLOR_MAGENTA 0x05
#define COLOR_BROWN   0x06
#define COLOR_LGRAY   0x07
#define COLOR_DGRAY   0x08
#define COLOR_LBLUE   0x09
#define COLOR_LGREEN  0x0A
#define COLOR_LCYAN   0x0B
#define COLOR_LRED    0x0C
#define COLOR_LMAGENTA 0x0D
#define COLOR_YELLOW  0x0E
#define COLOR_WHITE   0x0F

class GfxDriver {
public:
    static constexpr int FONT_W = 10;
    static constexpr int FONT_H = 16;
    static constexpr int FONT_CN_W = 16;

    GfxDriver() = default;

    void init();
    void clear(u8 color);
    void set_pixel(int x, int y, u8 color);
    void fill_rect(int x, int y, int w, int h, u8 color);
    void set_fg(u8 color);
    void set_bg(u8 color);

    void putc(char c);
    void puts(const char *s);
    void puts_utf8(const char *s);
    void cursor_draw();
    void cursor_erase();
    void put_hex_byte(u8 b);
    void put_hex_u32(u32 v);

private:
    u8  *fb_ = nullptr;
    int  w_ = 0, h_ = 0, pitch_ = 0, bpp_ = 0;
    int  cols_ = 0, rows_ = 0;
    int  cx_ = 0, cy_ = 0;
    u8   fg_ = 0x0F, bg_ = 0x00;
    u32  palette_[256]{};

    void init_palette();
    void draw_char(int sx, int sy, char c, u8 fg, u8 bg);
    void scroll();
    const unsigned char *font_cn_lookup(u16 cp);
    void draw_cn_char(int sx, int sy, u16 cp, u8 fg, u8 bg);
    static int utf8_decode(const char *s, u16 *cp);
};

/* Global GFX instance */
extern GfxDriver gfx;

/* C-compat wrappers */
inline void gfx_init()           { gfx.init(); }
inline void gfx_clear(u8 c)     { gfx.clear(c); }
inline void gfx_set_pixel(int x, int y, u8 c) { gfx.set_pixel(x, y, c); }
inline void gfx_fill_rect(int x, int y, int w, int h, u8 c) { gfx.fill_rect(x, y, w, h, c); }
inline void gfx_set_fg(u8 c)    { gfx.set_fg(c); }
inline void gfx_set_bg(u8 c)    { gfx.set_bg(c); }
inline void gfx_putc(char c)    { gfx.putc(c); }
inline void gfx_puts(const char *s) { gfx.puts(s); }
inline void gfx_puts_utf8(const char *s) { gfx.puts_utf8(s); }
inline void gfx_cursor_draw()   { gfx.cursor_draw(); }
inline void gfx_cursor_erase()  { gfx.cursor_erase(); }
inline void gfx_put_hex_byte(u8 b) { gfx.put_hex_byte(b); }
inline void gfx_put_hex_u32(u32 v) { gfx.put_hex_u32(v); }
