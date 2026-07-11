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

    char src_abs[512];
    char dst_abs[512];
    path_resolve(cwd, argv[0], src_abs);
    path_resolve(cwd, argv[1], dst_abs);

    FILE* src_fp = fopen(src_abs, "rb");
    if (!src_fp) {
        printf("cp: cannot open '%s'\n", src_abs);
        return;
    }

    fseek(src_fp, 0, SEEK_END);
    u32 size = (u32)ftell(src_fp);
    if (size == 0) {
        fclose(src_fp);
        printf("cp: '%s' is empty\n", src_abs);
        return;
    }
    fseek(src_fp, 0, SEEK_SET);

    u8* buf = (u8*)malloc(size);
    if (!buf) {
        fclose(src_fp);
        print("cp: out of memory\n");
        return;
    }

    size_t total_read = fread(buf, 1, size, src_fp);
    fclose(src_fp);

    if (total_read == 0) {
        free(buf);
        print("cp: failed to read file\n");
        return;
    }

    // If dst is a directory, append source filename
    char final_dst[512];
    strcpy(final_dst, dst_abs);

    u64 opendir_rc = syscall1(SYS_FS_OPENDIR, (u64)(usize)final_dst);
    if (opendir_rc == 0) {
        syscall0(SYS_FS_CLOSEDIR);
        size_t dlen = strlen(final_dst);
        if (dlen > 0 && final_dst[dlen - 1] != '/') {
            final_dst[dlen] = '/';
            final_dst[dlen + 1] = 0;
        }
        strcat(final_dst, path_basename(src_abs));
    }

    FILE* dst_fp = fopen(final_dst, "wb");
    if (!dst_fp) {
        free(buf);
        printf("cp: cannot create '%s'\n", final_dst);
        return;
    }

    size_t written = fwrite(buf, 1, total_read, dst_fp);
    fclose(dst_fp);
    free(buf);

    if (written == total_read)
        printf("Copied %u bytes: %s -> %s\n", (u32)total_read, src_abs, final_dst);
    else
        printf("cp: write error on '%s'\n", final_dst);
}
