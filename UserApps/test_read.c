#include "userlib.h"
#include "fcntl.h"
#include "unistd.h"

void main(const char* args, const char* cwd, i32 argc) {
    int fd = open("hello.c", O_RDONLY);
    printf("open returned fd: %d\n", fd);
    if (fd < 0) {
        print("Failed to open hello.c\n");
        return;
    }
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int n = read(fd, buf, 100);
    printf("Read returned %d bytes\n", n);
    printf("buf bytes: %d %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
    close(fd);
}
