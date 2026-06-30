#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    u64 mem = syscall2(SYS_SYSTEM, SYS_SYS_MEMINFO, 0);
    printf("Total usable memory: %u KB (%u MB)\n",
           (u32)(mem / 1024), (u32)(mem / (1024 * 1024)));
}