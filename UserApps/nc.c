#include "userlib.h"

u32 parse_ip(const char* str) {
    u32 ip = 0;
    u32 octet = 0;
    int shift = 0;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '.') {
            ip |= (octet << shift);
            shift += 8;
            octet = 0;
        } else if (str[i] >= '0' && str[i] <= '9') {
            octet = octet * 10 + (str[i] - '0');
        }
    }
    ip |= (octet << shift);
    return ip;
}

void main(const char* args, const char* cwd, i32 argc) {
    char* argv[16];
    char argbuf[256];
    i32 ac = 0;

    if (args && *args) {
        strcpy(argbuf, args);
        ac = parse_args(argbuf, argv, 16);
    }

    if (ac < 2) {
        printf("Usage: nc <ip> <port>\n");
        return;
    }

    u32 ip = parse_ip(argv[0]);
    u16 port = (u16)atoi(argv[1]);

    printf("Connecting to %s:%d...\n", argv[0], port);

    i32 sock = net_socket();
    if (sock < 0) {
        printf("Failed to create socket.\n");
        return;
    }

    i32 res = net_connect(sock, ip, port);
    if (res < 0) {
        printf("Failed to connect.\n");
        net_close(sock);
        return;
    }

    printf("Connected! Type messages and press Enter. Press Ctrl+C to exit.\n");

    char send_buf[256];
    int send_pos = 0;
    char recv_buf[256];

    while (1) {
        char c = getchar();
        if (c != 0) {
            if (c == 3) { // Ctrl+C
                break;
            } else if (c == '\n') {
                putchar('\n');
                send_buf[send_pos++] = '\n';
                net_send(sock, send_buf, send_pos);
                send_pos = 0;
            } else if (c == '\b') {
                if (send_pos > 0) {
                    send_pos--;
                    putchar('\b');
                }
            } else if (c >= 32 && c < 127) {
                if (send_pos < 255) {
                    send_buf[send_pos++] = c;
                    putchar(c);
                }
            }
        }

        i32 r = net_recv(sock, recv_buf, 255, 0); // non-blocking
        if (r > 0) {
            for (int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        }

        if (c == 0 && r == 0) {
            syscall1(SYS_SLEEP, 10);
        }
    }

    printf("Closing connection...\n");
    net_close(sock);
}
