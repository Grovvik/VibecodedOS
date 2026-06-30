#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char buf[256];
    u64 rc = syscall2(SYS_GETCWD, (u64)(usize)buf, (u64)sizeof(buf));
    if (rc == 0) {
        puts(buf);
    } else {
        print("/\n");
    }
}