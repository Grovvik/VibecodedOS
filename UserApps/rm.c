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
        print("Usage: rm <file>\n");
        return;
    }

    u64 rc = syscall1(SYS_FS_DELETE, (u64)(usize)argv[0]);
    if (rc == 0)
        printf("Deleted: %s\n", argv[0]);
    else
        printf("rm: failed to delete '%s'\n", argv[0]);
}
