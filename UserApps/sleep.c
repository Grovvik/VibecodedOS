#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char* argv[16];
    i32 ac = 0;
    char argbuf[512];

    if (args && *args) {
        strcpy(argbuf, args);
        ac = parse_args(argbuf, argv, 16);
    }

    if (ac < 1) {
        print("Usage: sleep <milliseconds>\n");
        return;
    }

    i32 ms = 0;
    const char* s = argv[0];
    while (*s >= '0' && *s <= '9') {
        ms = ms * 10 + (*s - '0');
        s++;
    }

    if (ms <= 0) {
        print("sleep: invalid duration\n");
        return;
    }

    printf("sleep %d ms (PID=%u)\n", ms, (u32)syscall0(SYS_GETPID));
    syscall1(SYS_SLEEP, (u64)ms);
    printf("sleep done\n");
}
