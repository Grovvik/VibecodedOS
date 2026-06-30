#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    print("Rebooting...\n");
    syscall2(SYS_SYSTEM, SYS_SYS_REBOOT, 0);
}