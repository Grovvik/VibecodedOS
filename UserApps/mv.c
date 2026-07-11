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

    FILE* src_fp = fopen(argv[0], "rb");
    if (!src_fp) {
        printf("mv: cannot open '%s'\n", argv[0]);
        return;
    }

    fseek(src_fp, 0, SEEK_END);
    u32 size = (u32)ftell(src_fp);
    if (size == 0) {
        fclose(src_fp);
        printf("mv: '%s' is empty\n", argv[0]);
        return;
    }
    fseek(src_fp, 0, SEEK_SET);

    u8* buf = (u8*)malloc(size);
    if (!buf) {
        fclose(src_fp);
        print("mv: out of memory\n");
        return;
    }

    size_t total_read = fread(buf, 1, size, src_fp);
    fclose(src_fp);

    if (total_read == 0) {
        free(buf);
        print("mv: failed to read file\n");
        return;
    }

    // Determine destination path (if dst is a directory, append src filename)
    char dest_path[512];
    const char* src_name = argv[0];
    const char* p = argv[0];
    while (*p) {
        if (*p == '/' || *p == '\\') src_name = p + 1;
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

    FILE* dst_fp = fopen(dest_path, "wb");
    if (!dst_fp) {
        free(buf);
        printf("mv: cannot create destination '%s'\n", dest_path);
        return;
    }

    size_t written = fwrite(buf, 1, total_read, dst_fp);
    fclose(dst_fp);
    free(buf);

    if (written != total_read) {
        printf("mv: failed to write '%s'\n", dest_path);
        return;
    }

    if (remove(argv[0]) != 0) {
        printf("mv: copied but failed to delete '%s'\n", argv[0]);
        return;
    }

    printf("Moved %u bytes: %s -> %s\n", (u32)total_read, argv[0], dest_path);
}
