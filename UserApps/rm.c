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

    char abs_path[512];
    path_resolve(cwd, argv[0], abs_path);

    if (remove(abs_path) == 0)
        printf("Deleted: %s\n", abs_path);
    else
        printf("rm: failed to delete '%s'\n", abs_path);
}
