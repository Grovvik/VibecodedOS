#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char* argv[16];
    i32 ac = 0;
    char argbuf[256];

    if (args && *args) {
        strcpy(argbuf, args);
        ac = parse_args(argbuf, argv, 16);
    }

    if (ac < 1) {
        print("Usage: cat <file>\n");
        return;
    }

    u64 rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)argv[0]);
    if (rc != 0) {
        printf("cat: cannot open '%s'\n", argv[0]);
        return;
    }

    u32 fsize = (u32)syscall0(SYS_FS_FILESIZE);
    char buf[1024];
    u32 pos = 0;
    while (pos < fsize) {
        u32 chunk = fsize - pos;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        u64 n = syscall2(SYS_FS_READFILE, (u64)(usize)buf, (u64)chunk);
        if (n == 0 || n == (u64)-1) break;
        for (u32 i = 0; i < (u32)n; i++) putchar(buf[i]);
        pos += (u32)n;
    }
    printf("\n");

    syscall0(SYS_FS_CLOSEFILE);
}