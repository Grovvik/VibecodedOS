#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    i32 ccolors[] = {FB_BLACK, FB_WHITE, FB_RED, FB_GREEN, FB_BLUE, FB_CYAN, FB_YELLOW, FB_MAGENTA, FB_GRAY};
    const char* cnames[] = {"Black","White","Red","Green","Blue","Cyan","Yellow","Magenta","Gray"};

    for (i32 i = 0; i < 9; i++) {
        setcolor(ccolors[i], FB_BLACK);
        printf("  %s  ", cnames[i]);
    }
    setcolor(FB_WHITE, FB_BLACK);
    putchar('\n');
    for (i32 i = 0; i < 9; i++) {
        setcolor(FB_BLACK, ccolors[i]);
        printf("  %s  ", cnames[i]);
    }
    setcolor(FB_WHITE, FB_BLACK);
    putchar('\n');
}