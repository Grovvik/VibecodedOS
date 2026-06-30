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
        print("Usage: cp <src> <dst>\n");
        return;
    }

    u64 open_rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)argv[0]);
    if (open_rc != 0) {
        printf("cp: cannot open '%s'\n", argv[0]);
        return;
    }

    u32 size = (u32)syscall0(SYS_FS_FILESIZE);
    if (size == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        printf("cp: '%s' is empty\n", argv[0]);
        return;
    }

    u8* buf = (u8*)0;
    u64 mmap_rc = syscall1(SYS_MMAP, (u64)size);
    if (mmap_rc == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        print("cp: out of memory\n");
        return;
    }
    buf = (u8*)mmap_rc;

    u32 total_read = 0;
    while (total_read < size) {
        u32 to_read = size - total_read;
        if (to_read > 4096) to_read = 4096;
        u64 n = syscall2(SYS_FS_READFILE, (u64)(usize)(buf + total_read), (u64)to_read);
        if (n == (u64)-1 || n == 0) break;
        total_read += (u32)n;
    }
    syscall0(SYS_FS_CLOSEFILE);

    if (total_read == 0) {
        print("cp: failed to read file\n");
        return;
    }

    u64 write_rc = syscall3(SYS_FS_WRITEFILE, (u64)(usize)argv[1], (u64)(usize)buf, (u64)total_read);
    if (write_rc == 0)
        printf("Copied %u bytes: %s -> %s\n", total_read, argv[0], argv[1]);
    else
        printf("cp: failed to write '%s'\n", argv[1]);
}
