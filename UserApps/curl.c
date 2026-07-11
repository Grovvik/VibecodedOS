/*
 * curl.c  —  MicroNT HTTP/HTTPS client (curl-compatible interface)
 *
 * Usage:
 *   curl [options] <url>
 *
 * Options:
 *   -X METHOD          HTTP method (GET, POST, PUT, DELETE, HEAD, ...) [default: GET]
 *   -H "Name: Value"   Add custom request header (can repeat up to 16x)
 *   -d "body"          Request body (implies POST if -X not set)
 *   --data-binary @-   Read body from stdin (placeholder: uses -d value)
 *   -o <file>          Save response body to file
 *   -O                 Save with remote filename (last URL segment)
 *   -v                 Verbose: print request + response headers to stderr
 *   -s                 Silent: suppress progress/errors
 *   -i                 Include response headers in output
 *   -I                 HEAD request only (prints headers)
 *   -L                 Follow HTTP redirects (up to 5)
 *   -k                 Insecure: skip TLS cert verification (default for now)
 *   --resolve host:port:ip   Override DNS for host:port with IP
 *   -u user:pass       Basic authentication (adds Authorization header)
 *   --max-time N       Total timeout in seconds [default: 30]
 *   -A "agent"         Custom User-Agent
 *
 * URL format:
 *   http://hostname[:port]/path
 *   https://hostname[:port]/path
 */

#include "userlib/userlib.h"
#include "userlib/tls_wrapper.h"

/* -------------------------------------------------------------------------
 * Configuration limits
 * ---------------------------------------------------------------------- */
#define CURL_MAX_HEADERS   16
#define CURL_MAX_URL       512
#define CURL_MAX_HOST      256
#define CURL_MAX_PATH      512
#define CURL_MAX_BODY      65536
#define CURL_MAX_RESPONSE  131072  /* 128 KB response buffer              */
#define CURL_MAX_REDIRECTS 5
#define CURL_RECV_TIMEOUT  30000   /* 30 s default                         */
#define CURL_BUF_SIZE      4096    /* per-read chunk                       */

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
static int  c_strlen(const char* s)             { int n=0; while(s[n]) n++; return n; }
static void c_strcpy(char* d, const char* s)    { while((*d++=*s++)); }
static int  c_strncmp(const char* a,const char* b,int n){ while(n--){if(*a!=*b)return*a-*b;if(!*a)return 0;a++;b++;}return 0; }
static char* c_strchr(const char* s,int c){while(*s&&*s!=(char)c)s++;return *s?(char*)s:0;}
static int  c_isdigit(int c){ return c>='0'&&c<='9'; }

static void c_strncpy_safe(char* dst, const char* src, int max) {
    int i = 0;
    while (i < max-1 && src[i]) { dst[i]=src[i]; i++; }
    dst[i] = 0;
}

/* Simple atoi for positive numbers */
static int c_atoi(const char* s) {
    int v = 0;
    while (c_isdigit((unsigned char)*s)) { v = v*10 + (*s-'0'); s++; }
    return v;
}

/* Low-level IP to string */
static void ip_to_str(u32 ip, char* buf) {
    /* ip is stored little-endian: byte0=low */
    snprintf(buf, 20, "%u.%u.%u.%u",
             ip & 0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
}

/* -------------------------------------------------------------------------
 * Parsed URL
 * ---------------------------------------------------------------------- */
typedef struct {
    int  is_https;
    char host[CURL_MAX_HOST];   /* hostname as given                      */
    u32  resolved_ip;            /* resolved IPv4 (0 = not resolved)       */
    u16  port;
    char path[CURL_MAX_PATH];
} ParsedUrl;

/* Returns 0 on success */
static int parse_url(const char* url, ParsedUrl* out) {
    out->is_https    = 0;
    out->port        = 80;
    out->resolved_ip = 0;
    out->host[0]     = 0;
    out->path[0]     = 0;

    const char* p = url;

    /* Protocol */
    if (c_strncmp(p, "https://", 8) == 0) {
        out->is_https = 1;
        out->port = 443;
        p += 8;
    } else if (c_strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else {
        /* no scheme — try as bare hostname or IP */
    }

    /* host[:port] */
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < CURL_MAX_HOST-1)
        out->host[hi++] = *p++;
    out->host[hi] = 0;

    if (*p == ':') {
        p++;
        int port = 0;
        while (c_isdigit((unsigned char)*p)) { port = port*10 + (*p-'0'); p++; }
        out->port = (u16)port;
    }

    /* path */
    if (*p == '/') {
        c_strncpy_safe(out->path, p, CURL_MAX_PATH);
    } else {
        out->path[0] = '/';
        out->path[1] = 0;
    }

    return (hi > 0) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Curl options
 * ---------------------------------------------------------------------- */
typedef struct {
    char  method[16];
    char* headers[CURL_MAX_HEADERS];
    int   header_count;
    char* body;                  /* request body (for POST/PUT)            */
    int   body_len;
    char  outfile[256];          /* -o filename                            */
    int   save_to_file;          /* -o or -O                              */
    int   use_remote_name;       /* -O                                     */
    int   verbose;               /* -v                                     */
    int   silent;                /* -s                                     */
    int   include_headers;       /* -i                                     */
    int   head_only;             /* -I                                     */
    int   follow_redirects;      /* -L                                     */
    int   insecure;              /* -k (always on for now)                 */
    int   max_time;              /* --max-time in seconds                  */
    char  user_agent[128];
    char  basic_auth[128];       /* -u user:pass                           */
    /* --resolve overrides */
    char  resolve_host[CURL_MAX_HOST];
    u16   resolve_port;
    u32   resolve_ip;
} CurlOpts;

/* -------------------------------------------------------------------------
 * Base64 for Basic Auth
 * ---------------------------------------------------------------------- */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const char* src, int slen, char* dst) {
    int di = 0;
    for (int i = 0; i < slen; ) {
        unsigned int b = 0;
        int rem = slen - i;
        b = (unsigned char)src[i++] << 16;
        if (rem > 1) b |= (unsigned char)src[i++] << 8;
        if (rem > 2) b |= (unsigned char)src[i++];
        dst[di++] = b64_table[(b>>18)&63];
        dst[di++] = b64_table[(b>>12)&63];
        dst[di++] = (rem>1) ? b64_table[(b>>6)&63]  : '=';
        dst[di++] = (rem>2) ? b64_table[(b>>0)&63]  : '=';
    }
    dst[di] = 0;
}

/* -------------------------------------------------------------------------
 * HTTP response parsing helpers
 * ---------------------------------------------------------------------- */
/* Find header value (case-insensitive name match) in response headers.
   resp_headers is a buffer of "Name: Value\r\n" lines.
   Returns pointer to value, or NULL. */
static const char* find_header(const char* buf, int buf_len,
                               const char* name) {
    int nlen = c_strlen(name);
    const char* p = buf;
    const char* end = buf + buf_len;
    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;
        /* case-insensitive prefix compare */
        int match = 1;
        if ((int)(line_end - p) <= nlen) { match = 0; }
        if (match) {
            for (int i = 0; i < nlen; i++) {
                char a = p[i]; if (a>='A'&&a<='Z') a+=32;
                char b = name[i]; if (b>='A'&&b<='Z') b+=32;
                if (a != b) { match = 0; break; }
            }
        }
        if (match && p[nlen] == ':') {
            const char* v = p + nlen + 1;
            while (v < line_end && *v == ' ') v++;
            return v;
        }
        p = line_end + 1;
    }
    return NULL;
}

static int parse_status_code(const char* resp) {
    /* "HTTP/1.1 200 OK\r\n" */
    const char* p = resp;
    while (*p && *p != ' ') p++;
    if (!*p) return 0;
    return c_atoi(p + 1);
}

/* Extract Location header value, copy to buf */
static int get_location(const char* headers, int hlen, char* buf, int bsz) {
    const char* v = find_header(headers, hlen, "location");
    if (!v) return 0;
    int i = 0;
    while (i < bsz-1 && v[i] && v[i] != '\r' && v[i] != '\n') {
        buf[i] = v[i]; i++;
    }
    buf[i] = 0;
    return i;
}

/* -------------------------------------------------------------------------
 * Build HTTP request string
 * ---------------------------------------------------------------------- */
static int build_request(const CurlOpts* opts, const ParsedUrl* url,
                         char* out, int out_size) {
    int pos = 0;

#define APPEND(s) do { \
    const char* _s = (s); int _l = c_strlen(_s); \
    if (pos + _l >= out_size) return -1; \
    for(int _i=0;_i<_l;_i++) out[pos++]=_s[_i]; \
} while(0)
#define APPENDN(s,n) do { \
    int _n=(n); \
    if (pos + _n >= out_size) return -1; \
    for(int _i=0;_i<_n;_i++) out[pos++]=(s)[_i]; \
} while(0)

    /* Request line */
    APPEND(opts->method);
    APPEND(" ");
    APPEND(url->path);
    APPEND(" HTTP/1.1\r\n");

    /* Host */
    APPEND("Host: ");
    APPEND(url->host);
    if ((url->is_https && url->port != 443) ||
        (!url->is_https && url->port != 80)) {
        char portbuf[8];
        snprintf(portbuf, 8, ":%u", url->port);
        APPEND(portbuf);
    }
    APPEND("\r\n");

    /* User-Agent */
    APPEND("User-Agent: ");
    APPEND(opts->user_agent[0] ? opts->user_agent : "MicroNT-curl/1.0");
    APPEND("\r\n");

    /* Accept */
    APPEND("Accept: */*\r\n");

    /* Connection */
    APPEND("Connection: close\r\n");

    /* Basic auth */
    if (opts->basic_auth[0]) {
        char encoded[256];
        base64_encode(opts->basic_auth, c_strlen(opts->basic_auth), encoded);
        APPEND("Authorization: Basic ");
        APPEND(encoded);
        APPEND("\r\n");
    }

    /* Content-Length + Content-Type for body */
    if (opts->body && opts->body_len > 0) {
        char lenbuf[16];
        snprintf(lenbuf, 16, "%d", opts->body_len);
        APPEND("Content-Length: ");
        APPEND(lenbuf);
        APPEND("\r\n");
        /* Default content-type if not overridden by user */
        int has_ct = 0;
        for (int i = 0; i < opts->header_count; i++) {
            if (c_strncmp(opts->headers[i], "Content-Type", 12) == 0 ||
                c_strncmp(opts->headers[i], "content-type", 12) == 0) {
                has_ct = 1; break;
            }
        }
        if (!has_ct)
            APPEND("Content-Type: application/x-www-form-urlencoded\r\n");
    }

    /* Custom headers */
    for (int i = 0; i < opts->header_count; i++) {
        APPEND(opts->headers[i]);
        APPEND("\r\n");
    }

    APPEND("\r\n");

    /* Body */
    if (opts->body && opts->body_len > 0) {
        APPENDN(opts->body, opts->body_len);
    }

    out[pos] = 0;
    return pos;
#undef APPEND
#undef APPENDN
}

/* -------------------------------------------------------------------------
 * Abstract I/O: plain TCP or TLS
 * ---------------------------------------------------------------------- */
typedef struct {
    int   sock;
    int   is_tls;
    tls_session_t* tls;
} Conn;

static int conn_send(Conn* c, const char* buf, int len) {
    if (c->is_tls) return tls_write(c->tls, buf, len);
    int sent = 0;
    while (sent < len) {
        int chunk = len - sent;
        if (chunk > 32767) chunk = 32767;
        int r = net_send(c->sock, buf + sent, (u16)chunk);
        if (r <= 0) return (sent > 0) ? sent : -1;
        sent += r;
    }
    return sent;
}

static int conn_recv(Conn* c, char* buf, int len, u32 timeout_ms) {
    if (c->is_tls) return tls_read(c->tls, buf, len);
    if (len > 32767) len = 32767;
    return net_recv(c->sock, buf, (u16)len, timeout_ms);
}

static void conn_close(Conn* c) {
    if (c->is_tls && c->tls) { tls_session_free(c->tls); c->tls = 0; }
    if (c->sock >= 0) { net_close(c->sock); c->sock = -1; }
}

/* -------------------------------------------------------------------------
 * Do one HTTP request → fills response_buf, returns status code or -1
 * ---------------------------------------------------------------------- */
static int do_request(const CurlOpts* opts, const ParsedUrl* url,
                      char* response_buf, int* response_len,
                      char* headers_buf,  int* headers_len,
                      int   verbose, int   silent) {
    /* Build request */
    static char req_buf[8192];
    int req_len = build_request(opts, url, req_buf, sizeof(req_buf));
    if (req_len < 0) {
        if (!silent) printf("curl: request too large\n");
        return -1;
    }

    if (verbose) {
        /* Print request lines prefixed with '>' */
        printf("> %s %s HTTP/1.1\n", opts->method, url->path);
        printf("> Host: %s\n", url->host);
        for (int i = 0; i < opts->header_count; i++)
            printf("> %s\n", opts->headers[i]);
        if (opts->body && opts->body_len > 0)
            printf("> [body: %d bytes]\n", opts->body_len);
        printf(">\n");
    }

    /* Connect */
    int sock = net_socket();
    if (sock < 0) {
        if (!silent) printf("curl: failed to create socket\n");
        return -1;
    }

    u32 ip = url->resolved_ip;
    if (ip == 0) {
        if (!silent) {
            char ipbuf[20];
            ip_to_str(0, ipbuf);
            printf("curl: could not resolve host '%s'\n", url->host);
        }
        net_close(sock);
        return -1;
    }

    if (!silent && verbose) {
        char ipbuf[20]; ip_to_str(ip, ipbuf);
        printf("* Connecting to %s (%s) port %u\n", url->host, ipbuf, url->port);
    }

    if (net_connect(sock, ip, url->port) < 0) {
        if (!silent) printf("curl: connection refused\n");
        net_close(sock);
        return -1;
    }

    if (!silent && verbose)
        printf("* Connected\n");

    /* TLS */
    Conn conn;
    conn.sock  = sock;
    conn.is_tls = url->is_https;
    conn.tls   = NULL;

    if (url->is_https) {
        if (tls_library_init() != 0) {
            if (!silent) printf("curl: TLS init failed\n");
            net_close(sock);
            return -1;
        }
        conn.tls = tls_session_create(sock, url->host);
        if (!conn.tls) {
            if (!silent) printf("curl: TLS session failed\n");
            net_close(sock);
            return -1;
        }
        if (tls_handshake(conn.tls) != 0) {
            if (!silent) printf("curl: TLS handshake failed\n");
            conn_close(&conn);
            return -1;
        }
        if (!silent && verbose) printf("* SSL connection established\n");
    }

    /* Send request */
    int sent = conn_send(&conn, req_buf, req_len);
    if (sent != req_len) {
        if (!silent) printf("curl: send failed (%d/%d)\n", sent, req_len);
        conn_close(&conn);
        return -1;
    }

    /* Receive response */
    u32 timeout_ms = (u32)opts->max_time * 1000;
    static char recv_chunk[CURL_BUF_SIZE];
    int total = 0;
    int max_resp = CURL_MAX_RESPONSE - 1;

    while (total < max_resp) {
        int r = conn_recv(&conn, recv_chunk, CURL_BUF_SIZE, timeout_ms);
        if (r <= 0) break;
        int copy = r;
        if (total + copy > max_resp) copy = max_resp - total;
        for (int i = 0; i < copy; i++) response_buf[total+i] = recv_chunk[i];
        total += copy;
    }
    response_buf[total] = 0;
    conn_close(&conn);

    if (total == 0) {
        if (!silent) printf("curl: empty response\n");
        return -1;
    }

    /* Split headers / body */
    int header_end = -1;
    for (int i = 0; i < total - 3; i++) {
        if (response_buf[i]=='\r' && response_buf[i+1]=='\n' &&
            response_buf[i+2]=='\r' && response_buf[i+3]=='\n') {
            header_end = i + 4;
            break;
        }
    }
    if (header_end < 0) header_end = total;

    *headers_len = header_end;
    if (headers_buf) {
        int hcopy = header_end < 4096 ? header_end : 4095;
        for (int i = 0; i < hcopy; i++) headers_buf[i] = response_buf[i];
        headers_buf[hcopy] = 0;
    }

    if (verbose) {
        /* Print response headers prefixed with '<' */
        int hi = 0;
        while (hi < header_end) {
            int line_start = hi;
            while (hi < header_end && response_buf[hi] != '\n') hi++;
            printf("< ");
            for (int k = line_start; k < hi && response_buf[k] != '\r'; k++)
                printf("%c", response_buf[k]);
            printf("\n");
            hi++;
        }
        printf("<\n");
    }

    *response_len = total;
    return parse_status_code(response_buf);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */
void main(const char* args, const char* cwd, i32 rargc) {
    char* argv[16];
    i32 argc = 0;
    char argbuf[512];

    if (args && *args) {
        strcpy(argbuf, args);
        argc = parse_args(argbuf, argv, 16);
    }

    if (argc < 2) {
        printf("Usage: curl [options] <url>\n");
        printf("  -X METHOD     HTTP method (GET, POST, PUT, DELETE, HEAD)\n");
        printf("  -H \"H: V\"     Add custom header (up to 16)\n");
        printf("  -d \"data\"     Request body (sets POST if -X not given)\n");
        printf("  -o file       Save output to file\n");
        printf("  -O            Save with remote filename\n");
        printf("  -v            Verbose output\n");
        printf("  -s            Silent mode\n");
        printf("  -i            Include response headers in output\n");
        printf("  -I            HEAD request only\n");
        printf("  -L            Follow redirects\n");
        printf("  -u user:pass  Basic authentication\n");
        printf("  -A \"agent\"    Custom User-Agent\n");
        printf("  --max-time N  Timeout in seconds\n");
        printf("  --resolve h:port:ip  Override DNS\n");
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Parse arguments                                                     */
    /* ------------------------------------------------------------------ */
    static CurlOpts opts;
    opts.header_count    = 0;
    opts.body            = NULL;
    opts.body_len        = 0;
    opts.save_to_file    = 0;
    opts.use_remote_name = 0;
    opts.verbose         = 0;
    opts.silent          = 0;
    opts.include_headers = 0;
    opts.head_only       = 0;
    opts.follow_redirects= 0;
    opts.insecure        = 1;  /* always for now */
    opts.max_time        = 30;
    opts.user_agent[0]   = 0;
    opts.basic_auth[0]   = 0;
    opts.resolve_host[0] = 0;
    opts.resolve_port    = 0;
    opts.resolve_ip      = 0;
    opts.method[0]       = 0;
    opts.outfile[0]      = 0;

    const char* url_str = NULL;
    static char header_storage[CURL_MAX_HEADERS][256];
    static char body_storage[CURL_MAX_BODY];

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (a[0] == '-' && a[1] == 'X') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val) c_strncpy_safe(opts.method, val, 16);

        } else if (a[0] == '-' && a[1] == 'H') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val && opts.header_count < CURL_MAX_HEADERS) {
                c_strncpy_safe(header_storage[opts.header_count], val, 256);
                opts.headers[opts.header_count++] = header_storage[opts.header_count];
                /* fix: re-assign after increment */
                opts.headers[opts.header_count-1] = header_storage[opts.header_count-1];
            }

        } else if (a[0] == '-' && a[1] == 'd') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val) {
                opts.body = body_storage;
                opts.body_len = c_strlen(val);
                if (opts.body_len > CURL_MAX_BODY-1) opts.body_len = CURL_MAX_BODY-1;
                for (int k = 0; k < opts.body_len; k++) body_storage[k] = val[k];
                body_storage[opts.body_len] = 0;
            }

        } else if (a[0] == '-' && a[1] == 'o') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val) {
                c_strncpy_safe(opts.outfile, val, 256);
                opts.save_to_file = 1;
            }

        } else if (a[0] == '-' && a[1] == 'O' && a[2] == 0) {
            opts.save_to_file    = 1;
            opts.use_remote_name = 1;

        } else if (a[0] == '-' && a[1] == 'v' && a[2] == 0) {
            opts.verbose = 1;

        } else if (a[0] == '-' && a[1] == 's' && a[2] == 0) {
            opts.silent = 1;

        } else if (a[0] == '-' && a[1] == 'i' && a[2] == 0) {
            opts.include_headers = 1;

        } else if (a[0] == '-' && a[1] == 'I' && a[2] == 0) {
            opts.head_only = 1;

        } else if (a[0] == '-' && a[1] == 'L' && a[2] == 0) {
            opts.follow_redirects = 1;

        } else if (a[0] == '-' && a[1] == 'k' && a[2] == 0) {
            opts.insecure = 1;

        } else if (a[0] == '-' && a[1] == 'u') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val) c_strncpy_safe(opts.basic_auth, val, 128);

        } else if (a[0] == '-' && a[1] == 'A') {
            const char* val = (a[2] ? a+2 : (i+1<argc ? argv[++i] : NULL));
            if (val) c_strncpy_safe(opts.user_agent, val, 128);

        } else if (c_strncmp(a, "--max-time", 10) == 0) {
            const char* val = (a[10]=='=' ? a+11 : (i+1<argc ? argv[++i] : NULL));
            if (val) opts.max_time = c_atoi(val);

        } else if (c_strncmp(a, "--resolve", 9) == 0) {
            /* --resolve host:port:ip */
            const char* val = (a[9]=='=' ? a+10 : (i+1<argc ? argv[++i] : NULL));
            if (val) {
                /* parse host:port:ip */
                int ci = 0;
                while (val[ci] && val[ci] != ':') {
                    opts.resolve_host[ci] = val[ci]; ci++;
                }
                opts.resolve_host[ci] = 0;
                if (val[ci] == ':') {
                    ci++;
                    opts.resolve_port = (u16)c_atoi(val + ci);
                    while (val[ci] && val[ci] != ':') ci++;
                    if (val[ci] == ':') ci++;
                    /* rest is IP — resolve via DNS or direct parse */
                    opts.resolve_ip = net_resolve(val + ci);
                }
            }

        } else if (a[0] == '-' && a[1] == '-') {
            /* unknown long option — skip */
        } else if (a[0] != '-') {
            url_str = a;
        }
    }

    if (!url_str) {
        printf("curl: no URL given\n");
        return;
    }

    /* Set default method */
    if (!opts.method[0]) {
        if (opts.head_only)                       c_strcpy(opts.method, "HEAD");
        else if (opts.body && opts.body_len > 0)  c_strcpy(opts.method, "POST");
        else                                       c_strcpy(opts.method, "GET");
    }
    if (opts.head_only && opts.method[0] == 'G') c_strcpy(opts.method, "HEAD");

    /* ------------------------------------------------------------------ */
    /* Parse URL and resolve hostname                                      */
    /* ------------------------------------------------------------------ */
    static ParsedUrl url;
    if (parse_url(url_str, &url) < 0) {
        printf("curl: invalid URL: %s\n", url_str);
        return;
    }

    /* Apply --resolve override */
    if (opts.resolve_host[0] &&
        c_strncmp(opts.resolve_host, url.host, CURL_MAX_HOST) == 0 &&
        opts.resolve_port == url.port) {
        url.resolved_ip = opts.resolve_ip;
    } else {
        if (!opts.silent)
            printf("* Resolving %s...\n", url.host);
        url.resolved_ip = net_resolve(url.host);
        if (url.resolved_ip == 0 && !opts.silent) {
            printf("curl: could not resolve host: %s\n", url.host);
            return;
        }
        if (!opts.silent && opts.verbose) {
            char ipbuf[20]; ip_to_str(url.resolved_ip, ipbuf);
            printf("* Resolved: %s\n", ipbuf);
        }
    }

    /* -O: derive filename from URL path */
    if (opts.use_remote_name) {
        const char* slash = url.path;
        const char* last  = slash;
        while (*slash) { if (*slash == '/') last = slash; slash++; }
        if (last[1]) c_strncpy_safe(opts.outfile, last+1, 256);
        else         c_strcpy(opts.outfile, "index.html");
    }

    /* ------------------------------------------------------------------ */
    /* Request loop (with redirect following)                              */
    /* ------------------------------------------------------------------ */
    static char  response[CURL_MAX_RESPONSE];
    static char  hdr_buf[4096];
    int resp_len  = 0;
    int hdr_len   = 0;
    int status    = -1;
    int redirects = 0;

    while (1) {
        resp_len = 0;
        hdr_len  = 0;

        status = do_request(&opts, &url, response, &resp_len,
                            hdr_buf, &hdr_len,
                            opts.verbose, opts.silent);
        if (status < 0) return;

        /* Follow redirect? */
        if (opts.follow_redirects &&
            (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
            redirects < CURL_MAX_REDIRECTS) {
            char new_url[CURL_MAX_URL] = {0};
            if (get_location(hdr_buf, hdr_len, new_url, CURL_MAX_URL) > 0) {
                if (!opts.silent) printf("* Following redirect to: %s\n", new_url);
                if (parse_url(new_url, &url) < 0) {
                    printf("curl: bad redirect URL\n");
                    return;
                }
                url.resolved_ip = net_resolve(url.host);
                if (url.resolved_ip == 0) {
                    printf("curl: redirect host unreachable\n");
                    return;
                }
                /* GET after redirect unless 307/308 */
                if (status != 307 && status != 308)
                    c_strcpy(opts.method, "GET");
                redirects++;
                continue;
            }
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    /* Output                                                              */
    /* ------------------------------------------------------------------ */
    const char* body     = response + hdr_len;
    int         body_len = resp_len - hdr_len;
    if (body_len < 0) body_len = 0;

    if (!opts.silent && !opts.verbose) {
        /* Print minimal status line like real curl */
    }

    /* -i: prepend headers to output */
    if (opts.include_headers && !opts.head_only) {
        /* headers already in response[0..hdr_len] */
        body     = response;
        body_len = resp_len;
    }
    if (opts.head_only) {
        body     = response;
        body_len = hdr_len;
    }

    if (opts.save_to_file && opts.outfile[0]) {
        /* Write to file */
        FILE* f = fopen(opts.outfile, "w");
        if (!f) {
            printf("curl: cannot open '%s' for writing\n", opts.outfile);
        } else {
            fwrite(body, 1, body_len, f);
            fclose(f);
            if (!opts.silent)
                printf("\n  %% Total: %d bytes saved to '%s'\n",
                       body_len, opts.outfile);
        }
    } else {
        /* Print to stdout */
        for (int i = 0; i < body_len; i++) printf("%c", body[i]);
    }
    printf("\n");
}
