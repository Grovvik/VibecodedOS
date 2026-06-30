#include "userlib.h"

// Вспомогательная функция для рисования пикселя
// Предполагаем формат BPP=32 (4 байта на пиксель: BGRA или RGBA)
void draw_pixel(u32* fb, u32 x, u32 y, u32 width, u32 color) {
    fb[y * width + x] = color;
}

void main(const char* args, const char* cwd, i32 argc) {
    FbInfo fb_info;

    i32 result = fb_map(&fb_info);

    if (result != 0) {
        printf("Error: Could not map framebuffer (result=%d)\n", result);
        return;
    }

    printf("Framebuffer mapped at: 0x%x\n", (u32)fb_info.fb_virt);
    printf("Resolution: %ux%u, BPP: %u\n", fb_info.width, fb_info.height, fb_info.bpp);

    u32* fb = (u32*)(usize)fb_info.fb_virt;

    u32 color = 0x0000FF;

    for (u32 y = 0; y < fb_info.height / 2; y++) {
        for (u32 x = 0; x < fb_info.width; x++) {
            draw_pixel(fb, x, y, fb_info.width, color);
        }
    }

    for (u32 i = 0; i < 200; i++) {
        draw_pixel(fb, 100 + i, 100 + i, fb_info.width, 0xFF0000);
    }

    printf("Drawing complete. Press any key to exit...\n");
    getchar();
}