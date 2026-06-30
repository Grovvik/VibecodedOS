#ifndef _KERNEL_FRAMEBUFFER_H_
#define _KERNEL_FRAMEBUFFER_H_

#include "types.h"

typedef struct {
    u32 fg;
    u32 bg;
} FbColor;

#define FB_WHITE   0x00FFFFFF
#define FB_BLACK   0x00000000
#define FB_GRAY    0x00808080
#define FB_RED     0x00FF0000
#define FB_GREEN   0x0000FF00
#define FB_BLUE    0x000000FF
#define FB_CYAN    0x0000FFFF
#define FB_YELLOW  0x00FFFF00
#define FB_MAGENTA 0x00FF00FF

void KeInitFramebuffer(void* fb_base, u32 width, u32 height, u32 pitch, u32 bpp);
void FbPutChar(char c);
void FbPrintString(const char* s);
void FbPrintf(const char* fmt, ...);
void FbClear(void);
void FbSetColor(u32 fg, u32 bg);
void FbSetCursorPos(u32 x, u32 y);
u32  FbGetCursorX(void);
u32  FbGetCursorY(void);
void FbScrollUp(u32 lines);
void FbDrawCharAt(char c, u32 x, u32 y, u32 fg, u32 bg);

#endif
