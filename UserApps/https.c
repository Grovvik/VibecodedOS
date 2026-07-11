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
        printf("Usage: https <ip> <host> [port] [path]\n");
        printf("Example: https 104.21.19.123 ip.ping.pe 443 /\n");
        return;
    }

    const char* ip_str = argv[0];
    const char* host_str = argv[1];
    u16 port = 443;
    const char* path_str = "/";

    if (ac >= 3) {
        port = (u16)atoi(argv[2]);
    }
    if (ac >= 4) {
        path_str = argv[3];
    }

    u32 ip = parse_ip(ip_str);

    printf("Connecting to %s:%d (Host: %s)...\n", ip_str, port, host_str);

    int sock = net_socket();
    if (sock < 0) {
        printf("Failed to create socket.\n");
        return;
    }

    int conn_res = net_connect(sock, ip, port);
    if (conn_res < 0) {
        printf("Failed to establish TCP connection.\n");
        net_close(sock);
        return;
    }

    printf("TCP connected. Initializing SSL/TLS library...\n");
    if (tls_library_init() != 0) {
        printf("Failed to initialize TLS library.\n");
        net_close(sock);
        return;
    }

    printf("Creating TLS session (SNI: %s)...\n", host_str);
    tls_session_t* session = tls_session_create(sock, host_str);
    if (!session) {
        printf("Failed to create TLS session.\n");
        net_close(sock);
        return;
    }

    printf("Starting TLS handshake...\n");
    int handshake_res = tls_handshake(session);
    if (handshake_res != 0) {
        printf("TLS handshake failed (code %d).\n", handshake_res);
        tls_session_free(session);
        net_close(sock);
        return;
    }

    printf("TLS Handshake completed successfully!\n");
    printf("Sending HTTP GET request...\n\n");

    /* Format standard HTTP/1.1 request */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: MicroNT/0.4 https client\r\n"
        "Connection: close\r\n\r\n",
        path_str, host_str
    );

    int written = tls_write(session, req, req_len);
    if (written <= 0) {
        printf("Failed to send TLS data.\n");
        tls_session_free(session);
        net_close(sock);
        return;
    }

    /* Read and print response */
    char recv_buf[512];
    int total_bytes = 0;
    while (1) {
        int read_res = tls_read(session, recv_buf, sizeof(recv_buf) - 1);
        if (read_res > 0) {
            recv_buf[read_res] = 0;
            printf("%s", recv_buf);
            total_bytes += read_res;
        } else if (read_res == 0) {
            /* Clean EOF from peer */
            break;
        } else {
            /* Error */
            printf("\n[TLS] Read error (code %d)\n", read_res);
            break;
        }
    }

    printf("\n\nConnection closed. Received %d bytes.\n", total_bytes);

    tls_session_free(session);
    net_close(sock);
}
