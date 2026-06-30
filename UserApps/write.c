#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char* argv[16];
    i32 ac = 0;
    char argbuf[512];

    if (args && *args) {
        strcpy(argbuf, args);
        ac = parse_args(argbuf, argv, 16);
    }

    if (ac < 2) {
        print("Usage: write <file> <text>\n");
        return;
    }

    char content[512];
    memset(content, 0, 512);
    u32 pos = 0;
    for (i32 i = 1; i < ac; i++) {
        if (i > 1 && pos < 511) content[pos++] = ' ';
        u32 slen = strlen(argv[i]);
        if (pos + slen >= 511) break;
        memcpy(content + pos, argv[i], slen);
        pos += slen;
    }
    if (pos < 511) content[pos++] = '\n';

    u64 rc = syscall3(SYS_FS_WRITEFILE, (u64)(usize)argv[0], (u64)(usize)content, (u64)pos);
    if (rc == 0)
        printf("Wrote %u bytes to %s\n", pos, argv[0]);
    else
        printf("Write failed\n");
}
