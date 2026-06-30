#include "framebuffer.h"
#include "font8x16.h"
#include "debug.h"
#include "runtime.h"
#include <stdarg.h>

static u8*  g_fb_base;
static u32  g_fb_width;
static u32  g_fb_height;
static u32  g_fb_pitch;
static u32  g_fb_bpp;
static u32  g_fb_bytes_per_pixel;

static u32  g_cursor_x;
static u32  g_cursor_y;
static FbColor g_fb_color = { FB_WHITE, FB_BLACK };

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

void KeInitFramebuffer(void* fb_base, u32 width, u32 height, u32 pitch, u32 bpp) {
    KdPrintf("[FB] Initializing framebuffer: base=%p %ux%u pitch=%u bpp=%u\n",
             fb_base, width, height, pitch, bpp);

    g_fb_base = (u8*)fb_base;
    g_fb_width = width;
    g_fb_height = height;
    g_fb_pitch = pitch;
    g_fb_bpp = bpp;
    g_fb_bytes_per_pixel = bpp / 8;
    g_cursor_x = 0;
    g_cursor_y = 0;

    FbClear();
    KdPrintf("[FB] Framebuffer initialized OK (text: %ux%u chars)\n",
             width / FONT_WIDTH, height / FONT_HEIGHT);
}

static void FbDrawPixel(u32 x, u32 y, u32 color) {
    if (x >= g_fb_width || y >= g_fb_height) return;
    u8* pixel = g_fb_base + y * g_fb_pitch + x * g_fb_bytes_per_pixel;
    pixel[0] = (u8)(color & 0xFF);
    pixel[1] = (u8)((color >> 8) & 0xFF);
    pixel[2] = (u8)((color >> 16) & 0xFF);
}

void FbDrawCharAt(char c, u32 col, u32 row, u32 fg, u32 bg) {
    u8 ch = (u8)c;
    const u8* glyph = g_font8x16[ch];

    u32 start_x = col * FONT_WIDTH;
    u32 start_y = row * FONT_HEIGHT;

    for (u32 y = 0; y < FONT_HEIGHT; y++) {
        u8 row_bits = glyph[y];
        for (u32 x = 0; x < FONT_WIDTH; x++) {
            u32 color = (row_bits & (0x80 >> x)) ? fg : bg;
            FbDrawPixel(start_x + x, start_y + y, color);
        }
    }
}

void FbScrollUp(u32 lines) {
    u32 scroll_pixels = lines * FONT_HEIGHT;
    if (scroll_pixels >= g_fb_height) {
        FbClear();
        return;
    }

    u8* dst = g_fb_base;
    u8* src = g_fb_base + scroll_pixels * g_fb_pitch;
    usize copy_size = (g_fb_height - scroll_pixels) * g_fb_pitch;

    RtMemCopy(dst, src, copy_size);

    u8* clear_start = g_fb_base + (g_fb_height - scroll_pixels) * g_fb_pitch;
    usize clear_size = scroll_pixels * g_fb_pitch;
    RtMemSet(clear_start, 0, clear_size);
}

void FbPutChar(char c) {
    if (!g_fb_base) return;

    if (c == '\n') {
        g_cursor_x = 0;
        g_cursor_y++;
    } else if (c == '\r') {
        g_cursor_x = 0;
    } else if (c == '\t') {
        g_cursor_x = (g_cursor_x + 4) & ~3;
        if (g_cursor_x >= g_fb_width / FONT_WIDTH) {
            g_cursor_x = 0;
            g_cursor_y++;
        }
    } else if (c == '\b') {
        if (g_cursor_x > 0) {
            g_cursor_x--;
            FbDrawCharAt(' ', g_cursor_x, g_cursor_y, g_fb_color.fg, g_fb_color.bg);
        }
    } else {
        FbDrawCharAt(c, g_cursor_x, g_cursor_y, g_fb_color.fg, g_fb_color.bg);
        g_cursor_x++;
    }

    u32 max_cols = g_fb_width / FONT_WIDTH;
    u32 max_rows = g_fb_height / FONT_HEIGHT;

    if (g_cursor_x >= max_cols) {
        g_cursor_x = 0;
        g_cursor_y++;
    }

    if (g_cursor_y >= max_rows) {
        FbScrollUp(1);
        g_cursor_y = max_rows - 1;
    }
}

void FbPrintString(const char* s) {
    if (!s) return;
    while (*s) FbPutChar(*s++);
}

void FbSetColor(u32 fg, u32 bg) {
    g_fb_color.fg = fg;
    g_fb_color.bg = bg;
}

void FbSetCursorPos(u32 x, u32 y) {
    g_cursor_x = x;
    g_cursor_y = y;
}

u32 FbGetCursorX(void) { return g_cursor_x; }
u32 FbGetCursorY(void) { return g_cursor_y; }

void FbClear(void) {
    if (!g_fb_base) return;
    RtMemSet(g_fb_base, 0, (usize)g_fb_height * g_fb_pitch);
    g_cursor_x = 0;
    g_cursor_y = 0;
}

void FbPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[512];
    char* p = buf;
    const char* f = fmt;

    while (*f && (p - buf) < 500) {
        if (*f != '%') { *p++ = *f++; continue; }
        f++;
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '0' || *f == '#' ||
               (*f >= '1' && *f <= '9') || *f == '.' || *f == '*') {
            if (*f == '*') { va_arg(args, int); }
            f++;
        }
        i32 is_long = 0;
        if (*f == 'l') { is_long = 1; f++; }
        if (*f == 'l') { is_long = 2; f++; }

        switch (*f) {
        case '%': *p++ = '%'; break;
        case 'c': { char c = (char)va_arg(args, int); *p++ = c; break; }
        case 's': {
            const char* s = va_arg(args, const char*);
            if (!s) s = "(null)";
            while (*s && (p - buf) < 500) *p++ = *s++;
            break;
        }
        case 'd': case 'i': {
            i64 v = is_long >= 2 ? va_arg(args, i64) :
                    is_long ? va_arg(args, long) : va_arg(args, int);
            char n[22]; RtItoa(v, n, 10);
            const char* np = n; while (*np) *p++ = *np++;
            break;
        }
        case 'u': {
            u64 v = is_long >= 2 ? va_arg(args, u64) :
                    is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned);
            char n[22]; RtUtoa(v, n, 10);
            const char* np = n; while (*np) *p++ = *np++;
            break;
        }
        case 'x': case 'X': case 'p': {
            u64 v;
            if (*f == 'p') { v = (u64)(usize)va_arg(args, void*); *p++='0'; *p++='x'; }
            else { v = is_long >= 2 ? va_arg(args, u64) :
                     is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned); }
            char n[18]; RtUtoa(v, n, 16);
            const char* np = n; while (*np) *p++ = *np++;
            break;
        }
        default: *p++ = '%'; *p++ = *f; break;
        }
        f++;
    }
    *p = 0;
    va_end(args);
    FbPrintString(buf);
}
