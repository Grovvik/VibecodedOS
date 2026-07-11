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
        char abs_path[512];
        path_resolve(cwd, argv[0], abs_path);

        // Try as directory first
        u64 rc = syscall1(SYS_FS_OPENDIR, (u64)(usize)abs_path);
        if (rc != 0) {
            // Try as a file
            FILE* f = fopen(abs_path, "rb");
            if (!f) {
                printf("ls: cannot open '%s'\n", abs_path);
                return;
            }
            fseek(f, 0, SEEK_END);
            u32 size = (u32)ftell(f);
            fclose(f);

            setcolor(FB_CYAN, FB_BLACK);
            print("File info:\n");
            setcolor(FB_WHITE, FB_BLACK);
            printf("  %s  %u bytes\n", path_basename(abs_path), size);
            return;
        }
        // fall through: directory is now open
    } else {
        // No argument — list current directory
        u64 rc = syscall1(SYS_FS_OPENDIR, 0);
        if (rc != 0) {
            print("ls: cannot open directory\n");
            return;
        }
    }

    setcolor(FB_CYAN, FB_BLACK);
    print("Directory listing:\n");
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