#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char os_version[64] = "unknown";
    syscall2(SYS_SYSTEM, SYS_SYS_VERSION, (u64)(usize)os_version);

    setcolor(FB_YELLOW, FB_BLACK);
    printf("MicroNT %s\n", os_version);
    setcolor(FB_WHITE, FB_BLACK);
}