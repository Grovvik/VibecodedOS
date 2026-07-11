#include "userlib.h"

void main(const char* args, const char* cwd, i32 argc) {
    char* argv[16];
    i32 ac = 0;
    char argbuf[256];

    if (args && *args) {
        strcpy(argbuf, args);
        ac = parse_args(argbuf, argv, 16);
    }

    if (ac > 0) {
        u64 rc = syscall1(SYS_FS_OPENDIR, (u64)(usize)argv[0]);
        if (rc != 0) {
            u64 file_rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)argv[0]);
            if (file_rc == 0) {
                u32 size = (u32)syscall0(SYS_FS_FILESIZE);
                syscall0(SYS_FS_CLOSEFILE);
                setcolor(FB_CYAN, FB_BLACK);
                print("Directory listing\n");
                setcolor(FB_WHITE, FB_BLACK);
                const char* base_name = argv[0];
                const char* p = argv[0];
                while (*p) {
                    if (*p == '/' || *p == '\\') base_name = p + 1;
                    p++;
                }
                printf("  %s  %u\n", base_name, size);
                return;
            }
            printf("ls: cannot open '%s'\n", argv[0]);
            return;
        }
    } else {
        u64 rc = syscall1(SYS_FS_OPENDIR, 0);
        if (rc != 0) {
            print("ls: cannot open directory\n");
            return;
        }
    }

    setcolor(FB_CYAN, FB_BLACK);
    print("Directory listing\n");
    setcolor(FB_WHITE, FB_BLACK);

    DirEntry entry;
    while (syscall1(SYS_FS_READDIR, (u64)(usize)&entry) == 0) {
        print("  ");
        if (entry.is_directory) {
            setcolor(FB_CYAN, FB_BLACK);
            printf("%s/", entry.name);
            setcolor(FB_WHITE, FB_BLACK);
        } else {
            printf("%s  %u", entry.name, entry.file_size);
        }
        putchar('\n');
    }

    syscall0(SYS_FS_CLOSEDIR);
}