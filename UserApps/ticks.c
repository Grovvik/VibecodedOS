#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    u64 ticks = syscall2(SYS_SYSTEM, SYS_SYS_TICKS, 0);
    printf("Tick count: %u (uptime: %u seconds)\n", (u32)ticks, (u32)(ticks / 100));
}