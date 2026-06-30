#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    print("System halted.\n");
    syscall2(SYS_SYSTEM, SYS_SYS_HALT, 0);
}