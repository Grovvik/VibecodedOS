#include "userlib.h"

#define SHELL_MAX_LINE 256
#define FAT_MAX_PATH   260

static char g_cwd[FAT_MAX_PATH] = "/";

static void shell_print_prompt(void) {
    setcolor(FB_GREEN, FB_BLACK);
    print("MicroNT");
    setcolor(FB_WHITE, FB_BLACK);
    putchar(':');
    setcolor(FB_CYAN, FB_BLACK);
    print(g_cwd);
    setcolor(FB_WHITE, FB_BLACK);
    print("> ");
}

static void shell_read_line(char* buf, u32 max) {
    u32 pos = 0;
    buf[0] = 0;

    while (1) {
        char c = getchar();

        if (c == 0) {
            syscall1(SYS_SLEEP, 10);
            continue;
        }

        if (c == '\n') {
            putchar('\n');
            buf[pos] = 0;
            return;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                buf[pos] = 0;
                putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            if (pos < max - 1) {
                buf[pos++] = c;
                buf[pos] = 0;
                putchar(c);
            }
        }
    }
}

static i32 shell_split_args(char* line, char** argv, i32 max_args) {
    i32 argc = 0;
    char* p = line;

    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    argv[argc] = 0;
    return argc;
}

static void shell_cmd_clear(void) {
    clear();
}

static void shell_cmd_echo(i32 argc, char** argv) {
    for (i32 i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        print(argv[i]);
    }
    putchar('\n');
}

static void shell_cmd_cd(i32 argc, char** argv) {
    if (argc < 2) {
        print(g_cwd);
        putchar('\n');
        return;
    }

    const char* target = argv[1];

    if (strcmp(target, "/") == 0 || strcmp(target, ".") == 0) {
        g_cwd[0] = '/';
        g_cwd[1] = 0;
        chdir(g_cwd);
        return;
    }

    if (strcmp(target, "..") == 0) {
        if (g_cwd[0] == '/' && g_cwd[1] == 0)
            return;
        char* last_slash = 0;
        char* p = g_cwd;
        while (*p) { if (*p == '/') last_slash = p; p++; }
        if (last_slash && last_slash > g_cwd)
            *last_slash = 0;
        else
            g_cwd[1] = 0;
        chdir(g_cwd);
        return;
    }

    char full_path[FAT_MAX_PATH];
    if (target[0] == '/') {
        strcpy(full_path, target);
    } else {
        strcpy(full_path, g_cwd);
        u32 len = strlen(full_path);
        if (len > 1 && full_path[len - 1] != '/') {
            full_path[len] = '/';
            full_path[len + 1] = 0;
        }
        strcat(full_path, target);
    }

    u64 rc = syscall1(SYS_FS_OPENDIR, (u64)(usize)full_path);
    if (rc != 0) {
        rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)full_path);
        if (rc == 0) {
            syscall0(SYS_FS_CLOSEFILE);
            setcolor(FB_RED, FB_BLACK);
            printf("cd: '%s': Not a directory\n", target);
            setcolor(FB_WHITE, FB_BLACK);
            return;
        }
        setcolor(FB_RED, FB_BLACK);
        printf("cd: '%s': No such file or directory\n", target);
        setcolor(FB_WHITE, FB_BLACK);
        return;
    }
    syscall0(SYS_FS_CLOSEDIR);
    strcpy(g_cwd, full_path);
    chdir(full_path);
}

static void shell_cmd_pwd(void) {
    print(g_cwd);
    putchar('\n');
}

static void shell_cmd_help(void) {
    setcolor(FB_CYAN, FB_BLACK);
    print("Available commands:\n");
    setcolor(FB_WHITE, FB_BLACK);
    print("  clear    - Clear screen\n");
    print("  echo     - Print text\n");
    print("  cd       - Change directory\n");
    print("  pwd      - Print working directory\n");
    print("  help     - Show this help\n");
    print("  ls       - List directory\n");
}

static void resolve_path(const char* user_path, char* resolved) {
    if (!user_path || !*user_path || user_path[0] == '/') {
        strcpy(resolved, user_path ? user_path : "");
        return;
    }
    u32 cwd_len = strlen(g_cwd);
    if (cwd_len == 1 && g_cwd[0] == '/') {
        resolved[0] = '/';
        strcpy(resolved + 1, user_path);
    } else {
        strcpy(resolved, g_cwd);
        if (resolved[cwd_len - 1] != '/') {
            resolved[cwd_len] = '/';
            resolved[cwd_len + 1] = 0;
        }
        strcat(resolved, user_path);
    }
}

static void shell_redirect_echo(i32 argc, char** argv, const char* outfile) {
    u32 total_len = 0;
    for (i32 i = 1; i < argc; i++) {
        total_len += strlen(argv[i]);
        if (i + 1 < argc) total_len++;
    }
    total_len++;

    u8* buf = (u8*)syscall1(SYS_MMAP, (u64)(total_len + 1));
    if (!buf) { print("echo: out of memory\n"); return; }

    u32 pos = 0;
    for (i32 i = 1; i < argc; i++) {
        u32 len = strlen(argv[i]);
        memcpy(buf + pos, argv[i], len);
        pos += len;
        if (i + 1 < argc) buf[pos++] = ' ';
    }
    buf[pos++] = '\n';

    char resolved[FAT_MAX_PATH];
    resolve_path(outfile, resolved);
    u64 rc = syscall3(SYS_FS_WRITEFILE, (u64)(usize)resolved, (u64)(usize)buf, (u64)pos);
    if (rc == 0)
        printf("Wrote %u bytes to %s\n", pos, outfile);
    else
        printf("Write failed\n");
}

static void shell_redirect_cat(const char* infile, const char* outfile) {
    u64 open_rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)infile);
    if (open_rc != 0) {
        printf("cat: cannot open '%s'\n", infile);
        return;
    }
    u32 size = (u32)syscall0(SYS_FS_FILESIZE);
    if (size == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        printf("cat: '%s' is empty\n", infile);
        return;
    }
    u8* buf = (u8*)syscall1(SYS_MMAP, (u64)size);
    if (!buf) {
        syscall0(SYS_FS_CLOSEFILE);
        print("cat: out of memory\n");
        return;
    }
    u32 total_read = 0;
    while (total_read < size) {
        u32 to_read = size - total_read;
        if (to_read > 4096) to_read = 4096;
        u64 n = syscall2(SYS_FS_READFILE, (u64)(usize)(buf + total_read), (u64)to_read);
        if (n == (u64)-1 || n == 0) break;
        total_read += (u32)n;
    }
    syscall0(SYS_FS_CLOSEFILE);

    if (total_read > 0) {
        char resolved[FAT_MAX_PATH];
        resolve_path(outfile, resolved);
        u64 rc = syscall3(SYS_FS_WRITEFILE, (u64)(usize)resolved, (u64)(usize)buf, (u64)total_read);
        if (rc == 0)
            printf("Copied %u bytes to %s\n", total_read, outfile);
        else
            printf("Write failed\n");
    }
}

static void shell_execute(char* line);

static int file_exists(const char* path) {
    u64 rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)path);
    if (rc == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        return 1;
    }
    return 0;
}

static void shell_run_bat(const char* filepath) {
    u64 open_rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)filepath);
    if (open_rc != 0) {
        printf("bat: cannot open '%s'\n", filepath);
        return;
    }

    u32 size = (u32)syscall0(SYS_FS_FILESIZE);
    if (size == 0) {
        syscall0(SYS_FS_CLOSEFILE);
        return;
    }

    u8* buf = (u8*)syscall1(SYS_MMAP, (u64)size + 1);
    if (!buf) {
        syscall0(SYS_FS_CLOSEFILE);
        print("bat: out of memory\n");
        return;
    }

    u32 total_read = 0;
    while (total_read < size) {
        u32 to_read = size - total_read;
        if (to_read > 4096) to_read = 4096;
        u64 n = syscall2(SYS_FS_READFILE, (u64)(usize)(buf + total_read), (u64)to_read);
        if (n == (u64)-1 || n == 0) break;
        total_read += (u32)n;
    }
    syscall0(SYS_FS_CLOSEFILE);
    buf[total_read] = 0;

    char* line_start = (char*)buf;
    char* p = (char*)buf;
    while (p < (char*)buf + total_read) {
        if (*p == '\n' || *p == '\r') {
            char old_char = *p;
            *p = 0;

            u32 len = strlen(line_start);
            while (len > 0 && (line_start[len - 1] == ' ' || line_start[len - 1] == '\t')) {
                line_start[len - 1] = 0;
                len--;
            }
            if (len > 0 && line_start[0] != '#' && line_start[0] != ';' &&
                strncmp(line_start, "rem ", 4) != 0 && strcmp(line_start, "rem") != 0) {

                setcolor(FB_YELLOW, FB_BLACK);
                printf("> %s\n", line_start);
                setcolor(FB_WHITE, FB_BLACK);

                shell_execute(line_start);
            }

            *p = old_char;
            line_start = p + 1;
        }
        p++;
    }

    if (line_start < (char*)buf + total_read) {
        u32 len = strlen(line_start);
        if (len > 0 && line_start[0] != '#' && line_start[0] != ';' &&
            strncmp(line_start, "rem ", 4) != 0 && strcmp(line_start, "rem") != 0) {

            setcolor(FB_YELLOW, FB_BLACK);
            printf("> %s\n", line_start);
            setcolor(FB_WHITE, FB_BLACK);

            shell_execute(line_start);
        }
    }
}

static void shell_auto_run(i32 argc, char** argv) {
    char prog_name[SHELL_MAX_LINE];
    u32 len = strlen(argv[0]);
    if (len >= SHELL_MAX_LINE - 5) {
        print("Program name too long\n");
        return;
    }
    memcpy(prog_name, argv[0], len);
    prog_name[len] = 0;

    int is_path = 0;
    for (u32 i = 0; i < len; i++) {
        if (prog_name[i] == '/' || prog_name[i] == '\\' || prog_name[i] == '.') {
            is_path = 1;
            break;
        }
    }

    char args_buf[512];
    args_buf[0] = 0;
    char* ap = args_buf;
    for (i32 a = 1; a < argc && (u32)(ap - args_buf) < 500; a++) {
        if (a > 1) *ap++ = ' ';
        u32 alen = strlen(argv[a]);
        memcpy(ap, argv[a], alen);
        ap += alen;
    }
    *ap = 0;

    char resolved_path[FAT_MAX_PATH];
    int run_as_bat = 0;
    int run_as_exe = 0;

    if (is_path) {
        resolve_path(prog_name, resolved_path);
        u32 rplen = strlen(resolved_path);
        if (rplen >= 4 && strcmp(resolved_path + rplen - 4, ".bat") == 0) {
            if (file_exists(resolved_path)) {
                run_as_bat = 1;
            }
        } else if (rplen >= 4 && strcmp(resolved_path + rplen - 4, ".exe") == 0) {
            if (file_exists(resolved_path)) {
                run_as_exe = 1;
            }
        } else {
            // Check with suffix
            char check_path[FAT_MAX_PATH];
            strcpy(check_path, resolved_path);
            strcat(check_path, ".bat");
            if (file_exists(check_path)) {
                strcpy(resolved_path, check_path);
                run_as_bat = 1;
            } else {
                strcpy(check_path, resolved_path);
                strcat(check_path, ".exe");
                if (file_exists(check_path)) {
                    strcpy(resolved_path, check_path);
                    run_as_exe = 1;
                } else {
                    // Just try as is
                    run_as_exe = 1;
                }
            }
        }
    } else {
        // Try local directories
        char check_path[FAT_MAX_PATH];
        
        // Local .bat
        resolve_path(prog_name, check_path);
        strcat(check_path, ".bat");
        if (file_exists(check_path)) {
            strcpy(resolved_path, check_path);
            run_as_bat = 1;
        } else {
            // Local .exe
            resolve_path(prog_name, check_path);
            strcat(check_path, ".exe");
            if (file_exists(check_path)) {
                strcpy(resolved_path, check_path);
                run_as_exe = 1;
            } else {
                // /bin/ .bat
                strcpy(check_path, "/bin/");
                strcat(check_path, prog_name);
                strcat(check_path, ".bat");
                if (file_exists(check_path)) {
                    strcpy(resolved_path, check_path);
                    run_as_bat = 1;
                } else {
                    // /bin/ .exe
                    strcpy(check_path, "/bin/");
                    strcat(check_path, prog_name);
                    strcat(check_path, ".exe");
                    strcpy(resolved_path, check_path);
                    run_as_exe = 1;
                }
            }
        }
    }

    if (run_as_bat) {
        shell_run_bat(resolved_path);
    } else if (run_as_exe) {
        u64 pid = syscall2(SYS_EXEC, (u64)(usize)resolved_path, (u64)(usize)args_buf);
        if (pid == (u64)-1) {
            setcolor(FB_RED, FB_BLACK);
            printf("Unknown command: %s\n", argv[0]);
            setcolor(FB_WHITE, FB_BLACK);
            print("Type 'help' for available commands\n");
        } else {
            syscall1(SYS_WAITPID, pid);
        }
    }
}

static void shell_execute(char* line) {
    char* redirect_file = 0;
    char cmd_line[SHELL_MAX_LINE];

    memcpy(cmd_line, line, SHELL_MAX_LINE);

    char* gt = cmd_line;
    while (*gt) {
        if (*gt == '>') {
            *gt = 0;
            gt++;
            while (*gt == ' ') gt++;
            redirect_file = gt;
            while (*gt && *gt != ' ' && *gt != '\t') gt++;
            *gt = 0;
            break;
        }
        gt++;
    }

    char* argv[16];
    i32 argc = shell_split_args(cmd_line, argv, 16);

    if (argc == 0) return;

    if (redirect_file && strcmp(argv[0], "echo") == 0) {
        shell_redirect_echo(argc, argv, redirect_file);
        return;
    }

    if (redirect_file && strcmp(argv[0], "cat") == 0) {
        if (argc < 2) { print("Usage: cat <file> > <outfile>\n"); return; }
        shell_redirect_cat(argv[1], redirect_file);
        return;
    }

    if (strcmp(argv[0], "clear") == 0) shell_cmd_clear();
    else if (strcmp(argv[0], "echo") == 0) shell_cmd_echo(argc, argv);
    else if (strcmp(argv[0], "cd") == 0) shell_cmd_cd(argc, argv);
    else if (strcmp(argv[0], "pwd") == 0) shell_cmd_pwd();
    else if (strcmp(argv[0], "help") == 0) shell_cmd_help();
    else shell_auto_run(argc, argv);
}

void main(const char* args, const char* cwd, i32 argc) {
    if (cwd && *cwd) {
        strcpy(g_cwd, cwd);
    }

    setcolor(FB_CYAN, FB_BLACK);
    print("\n========================================\n");
    print("  ");
    setcolor(FB_YELLOW, FB_BLACK);
    print("MicroNT v0.3 (Phase 2 - Ring3 + AHCI + FAT)");
    setcolor(FB_CYAN, FB_BLACK);
    print("\n========================================\n\n");
    setcolor(FB_WHITE, FB_BLACK);

    char line[SHELL_MAX_LINE];

    // Auto-run DOOM for testing
    //{
    //    char* auto_argv[4];
    //    auto_argv[0] = "doom";
    //    auto_argv[1] = "-iwad";
    //    auto_argv[2] = "/bin/doom1.wad";
    //    auto_argv[3] = 0;
    //    shell_auto_run(3, auto_argv);
    //}

    while (1) {
        shell_print_prompt();
        shell_read_line(line, SHELL_MAX_LINE);
        shell_execute(line);
    }
}
