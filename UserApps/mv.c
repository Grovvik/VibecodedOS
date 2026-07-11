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
        print("Usage: mv <src> <dst>\n");
        return;
    }

    u64 open_rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)argv[0]);
    if (open_rc != 0) {
        printf("mv: cannot open '%s'\n", argv[0]);
        return;
    }

    u32 size = (u32)syscall0(SYS_FS_FILESIZE);
    if (size == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        printf("mv: '%s' is empty\n", argv[0]);
        return;
    }

    u8* buf = (u8*)0;
    u64 mmap_rc = syscall1(SYS_MMAP, (u64)size);
    if (mmap_rc == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        print("mv: out of memory\n");
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
        print("mv: failed to read file\n");
        return;
    }

    char dest_path[512];
    const char* src_name = argv[0];
    const char* p = argv[0];
    while (*p) {
        if (*p == '/' || *p == '\\') {
            src_name = p + 1;
        }
        p++;
    }

    u64 opendir_rc = syscall1(SYS_FS_OPENDIR, (u64)(usize)argv[1]);
    if (opendir_rc == 0) {
        syscall0(SYS_FS_CLOSEDIR);
        strcpy(dest_path, argv[1]);
        u32 len = strlen(dest_path);
        if (len > 0 && dest_path[len - 1] != '/' && dest_path[len - 1] != '\\') {
            dest_path[len] = '/';
            dest_path[len + 1] = 0;
        }
        strcat(dest_path, src_name);
    } else {
        strcpy(dest_path, argv[1]);
    }

    u64 write_rc = syscall3(SYS_FS_WRITEFILE, (u64)(usize)dest_path, (u64)(usize)buf, (u64)total_read);
    if (write_rc != 0) {
        printf("mv: failed to write '%s'\n", dest_path);
        return;
    }

    u64 del_rc = syscall1(SYS_FS_DELETE, (u64)(usize)argv[0]);
    if (del_rc != 0) {
        printf("mv: copied but failed to delete '%s'\n", argv[0]);
        return;
    }

    printf("Moved %u bytes: %s -> %s\n", total_read, argv[0], dest_path);
}
