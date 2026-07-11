#include "userlib.h"
#include <stdarg.h>

#define PAGE_SIZE 4096
#ifndef NULL
#define NULL ((void*)0)
#endif
#ifndef EOF
#define EOF (-1)
#endif

// ============================================================================
// ����� 1: ����������� ��� �� ������� (ORIGINAL USERLIB)
// ============================================================================

void print(const char* s) {
    syscall1(SYS_PUTS, (u64)(usize)s);
}

void puts(const char* s) {
    print(s);
    putchar('\n');
}

void putchar(char c) {
    char buf[2];
    buf[0] = c;
    buf[1] = 0;
    print(buf);
}

void clear(void) {
    syscall0(SYS_CLEAR);
}

void setcolor(i32 fg, i32 bg) {
    syscall2(SYS_SETCOLOR, (u64)fg, (u64)bg);
}

char getchar(void) {
    return (char)syscall0(SYS_GETCHAR);
}

void chdir(const char* path) {
    syscall1(SYS_CHDIR, (u64)(usize)path);
}

u64 waitpid(u64 pid) {
    return syscall1(SYS_WAITPID, pid);
}

i32 fb_map(FbInfo* info) {
    return (i32)syscall1(SYS_FB_MAP, (u64)(usize)info);
}

i32 getkey(KeyEvent* ev) {
    return (i32)syscall1(SYS_GETKEY, (u64)(usize)ev);
}

static char* fd_paths[128] = {0};

i32 fd_open(const char* path) {
    i32 fd = (i32)syscall1(SYS_OPEN, (u64)(usize)path);
    if (fd >= 0 && fd < 128) {
        if (fd_paths[fd]) free(fd_paths[fd]);
        fd_paths[fd] = (char*)malloc(strlen(path) + 1);
        if (fd_paths[fd]) strcpy(fd_paths[fd], path);
    }
    return fd;
}

i32 fd_read(i32 fd, void* buf, u32 count) {
    return (i32)syscall3(SYS_READ, (u64)fd, (u64)(usize)buf, (u64)count);
}

i32 fd_lseek(i32 fd, i64 offset, i32 whence) {
    return (i32)syscall3(SYS_LSEEK, (u64)fd, (u64)offset, (u64)whence);
}

i32 fd_close(i32 fd) {
    if (fd >= 0 && fd < 128) {
        if (fd_paths[fd]) {
            free(fd_paths[fd]);
            fd_paths[fd] = NULL;
        }
    }
    return (i32)syscall1(SYS_CLOSEFD, (u64)fd);
}

i32 audio_init(void) {
    return (i32)syscall0(SYS_AUDIO_INIT);
}

i32 audio_play(const void* buf, u32 size) {
    return (i32)syscall2(SYS_AUDIO_PLAY, (u64)(usize)buf, (u64)size);
}

i32 audio_stop(void) {
    return (i32)syscall0(SYS_AUDIO_STOP);
}

i32 net_socket(void) {
    return (i32)syscall0(SYS_NET_OPEN);
}

i32 net_connect(i32 sock, u32 ip, u16 port) {
    return (i32)syscall3(SYS_NET_CONNECT, (u64)sock, (u64)ip, (u64)port);
}

i32 net_send(i32 sock, const void* buf, u16 len) {
    return (i32)syscall3(SYS_NET_SEND, (u64)sock, (u64)(usize)buf, (u64)len);
}

i32 net_recv(i32 sock, void* buf, u16 len, u32 timeout_ms) {
    return (i32)syscall4(SYS_NET_RECV, (u64)sock, (u64)(usize)buf, (u64)len, (u64)timeout_ms);
}

i32 net_close(i32 sock) {
    return (i32)syscall1(SYS_NET_CLOSE, (u64)sock);
}

// ============================================================================
// ����� 2: �������������� � ����� (ORIGINAL USERLIB)
// ============================================================================

void itoa(i64 val, char* buf, i32 base) {
    if (val < 0 && base == 10) {
        *buf++ = '-';
        val = -val;
    }
    utoa((u64)val, buf, base);
}

void utoa(u64 val, char* buf, i32 base) {
    char tmp[20];
    i32 i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (val > 0) {
        tmp[i++] = "0123456789abcdef"[val % base];
        val /= base;
    }
    while (i--) *buf++ = tmp[i];
    *buf = 0;
}

static char g_printf_buf[512];

void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* p = g_printf_buf;
    while (*fmt && (p - g_printf_buf) < 510) {
        if (*fmt != '%') { *p++ = *fmt++; continue; }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (s) while (*s) *p++ = *s++;
            break;
        }
        case 'd': {
            i32 val = va_arg(ap, i32);
            char num[20]; itoa((i64)val, num, 10);
            char* n = num; while (*n) *p++ = *n++;
            break;
        }
        case 'u': {
            u32 val = va_arg(ap, u32);
            char num[20]; utoa((u64)val, num, 10);
            char* n = num; while (*n) *p++ = *n++;
            break;
        }
        case 'x': {
            u32 val = va_arg(ap, u32);
            char num[20]; utoa((u64)val, num, 16);
            char* n = num; while (*n) *p++ = *n++;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, i32);
            *p++ = c;
            break;
        }
        case '%': *p++ = '%'; break;
        default: *p++ = '%'; *p++ = *fmt; break;
        }
        fmt++;
    }
    *p = 0;
    print(g_printf_buf);
    va_end(ap);
}

i32 parse_args(const char* args, char** argv, i32 max) {
    i32 argc = 0;
    const char* p = args;
    while (*p && argc < max - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = (char*)p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *((char*)p) = 0; p++; }
        else { break; }
    }
    argv[argc] = 0;
    return argc;
}

// ============================================================================
// ����� 3: ������� ������ � ����� (����������� �� CRT)
// ============================================================================

void* memcpy(void* dst, const void* src, usize n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, usize n) {
    u8* d = (u8*)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void* memmove(void* dst, const void* src, usize n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

int memcmp(const void* a, const void* b, usize n) {
    const u8* pa = (const u8*)a; const u8* pb = (const u8*)b;
    while (n--) { if (*pa != *pb) return *pa - *pb; pa++; pb++; }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int strncmp(const char* a, const char* b, usize n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (usize)-1) return 0;
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

char* strcpy(char* dst, const char* src) { char* d = dst; while ((*d++ = *src++)); return dst; }
char* strncpy(char* dst, const char* src, usize n) {
    char* d = dst;
    while (n-- && (*d++ = *src++));
    while (n-- > 0) *d++ = 0;
    return dst;
}

char* strcat(char* dst, const char* src) { char* d = dst + strlen(dst); while ((*d++ = *src++)); return dst; }
char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return c == 0 ? (char*)s : NULL;
}

size_t strspn(const char* s, const char* accept) {
    size_t n = 0; while (*s && strchr(accept, *s)) { s++; n++; } return n;
}

size_t strcspn(const char* s, const char* reject) {
    size_t n = 0; while (*s && !strchr(reject, *s)) { s++; n++; } return n;
}

// ������������� �������� (ctype)
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int atoi(const char* s) {
    int res = 0, sign = 1;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) { res = res * 10 + (*s - '0'); s++; }
    return res * sign;
}

int abs(int n) { return n < 0 ? -n : n; }

// ============================================================================
// ����� 4: ��������� ������ (���������� �� CRT)
// ============================================================================

#define MALLOC_ALIGN 16
#define MALLOC_MIN_BLOCK 64

typedef struct MallocHeader {
    usize size;
    int used;
    struct MallocHeader* next;
} MallocHeader;

static MallocHeader* malloc_head = NULL;

static void* malloc_from_mmap(usize size) {
    usize total = sizeof(MallocHeader) + size;
    total = (total + PAGE_SIZE - 1) & ~((usize)PAGE_SIZE - 1);

    u64 ptr = syscall1(SYS_MMAP, total); // ���������� syscall ��������
    if (!ptr) return NULL;

    MallocHeader* blk = (MallocHeader*)ptr;
    usize excess = total - sizeof(MallocHeader) - size;

    if (excess >= sizeof(MallocHeader) + MALLOC_MIN_BLOCK) {
        blk->size = size;
        blk->used = 1;
        blk->next = malloc_head;
        malloc_head = blk;
        MallocHeader* rest = (MallocHeader*)((u8*)blk + sizeof(MallocHeader) + size);
        rest->size = excess - sizeof(MallocHeader);
        rest->used = 0;
        rest->next = blk;
        malloc_head = rest;
    }
    else {
        blk->size = total - sizeof(MallocHeader);
        blk->used = 1;
        blk->next = malloc_head;
        malloc_head = blk;
    }
    return (void*)((u8*)blk + sizeof(MallocHeader));
}

void* malloc(usize size) {
    if (size == 0) return NULL;
    size = (size + MALLOC_ALIGN - 1) & ~((usize)MALLOC_ALIGN - 1);
    if (size < MALLOC_MIN_BLOCK) size = MALLOC_MIN_BLOCK;

    MallocHeader* best = NULL;
    MallocHeader* p = malloc_head;
    while (p) {
        if (!p->used && p->size >= size && (!best || p->size < best->size))
            best = p;
        p = p->next;
    }
    if (best) {
        if (best->size >= size + sizeof(MallocHeader) + MALLOC_MIN_BLOCK) {
            MallocHeader* rest = (MallocHeader*)((u8*)best + sizeof(MallocHeader) + size);
            rest->size = best->size - size - sizeof(MallocHeader);
            rest->used = 0;
            rest->next = best->next;
            best->size = size;
            best->next = rest;
        }
        best->used = 1;
        return (void*)((u8*)best + sizeof(MallocHeader));
    }
    return malloc_from_mmap(size);
}

void free(void* ptr) {
    if (!ptr) return;
    MallocHeader* blk = (MallocHeader*)((u8*)ptr - sizeof(MallocHeader));
    blk->used = 0;
}

void* calloc(usize count, usize size) {
    usize total = count * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

int errno = 0;

typedef struct _FILE {
    int fd;
    int eof;
    int err;
    int is_write;
    char* path;
    char* write_buf;
    size_t write_pos;
    size_t write_cap;
    size_t write_len;
} FILE;

static FILE _stdout = { .fd = 1, .is_write = 1 };
static FILE _stderr = { .fd = 2, .is_write = 1 };
static FILE _stdin  = { .fd = 0, .is_write = 0 };

FILE* stdout = &_stdout;
FILE* stderr = &_stderr;
FILE* stdin  = &_stdin;

FILE* fopen(const char* path, const char* mode) {
    int is_write = 0;
    if (strchr(mode, 'w') || strchr(mode, 'a')) {
        is_write = 1;
    }
    
    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp) return NULL;
    memset(fp, 0, sizeof(FILE));
    fp->is_write = is_write;
    
    if (is_write) {
        fp->path = (char*)malloc(strlen(path) + 1);
        if (!fp->path) {
            free(fp);
            return NULL;
        }
        strcpy(fp->path, path);
        fp->fd = -1;
    } else {
        int fd = fd_open(path);
        if (fd < 0) {
            free(fp);
            return NULL;
        }
        fp->fd = fd;
    }
    fp->eof = 0;
    fp->err = 0;
    return fp;
}

int fclose(FILE* fp) {
    if (!fp) return EOF;
    if (fp->is_write) {
        if (fp->path && fp->write_buf && fp->write_len > 0) {
            syscall3(SYS_FS_WRITEFILE, (u64)(usize)fp->path, (u64)(usize)fp->write_buf, (u64)fp->write_len);
        } else if (fp->path && fp->write_len == 0) {
            syscall3(SYS_FS_WRITEFILE, (u64)(usize)fp->path, (u64)(usize)"", 0);
        }
        if (fp->path) free(fp->path);
        if (fp->write_buf) free(fp->write_buf);
    } else {
        if (fp->fd >= 0) {
            fd_close(fp->fd);
        }
    }
    free(fp);
    return 0;
}

int feof(FILE* fp) { return fp ? fp->eof : 0; }
int ferror(FILE* fp) { return fp ? fp->err : 0; }

int fgetc(FILE* fp) {
    if (fp->fd == 0) {
        return (int)getchar();
    }
    unsigned char c;
    int r = fd_read(fp->fd, &c, 1);
    if (r <= 0) { fp->eof = 1; return EOF; }
    return (int)c;
}

char* fgets(char* buf, int n, FILE* fp) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(fp);
        if (c == EOF) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    return i > 0 ? buf : NULL;
}

size_t fread(void* buf, size_t sz, size_t cnt, FILE* fp) {
    u8* p = (u8*)buf;
    size_t total = sz * cnt;
    size_t done = 0;
    while (done < total) {
        int r = fd_read(fp->fd, p + done, (u32)(total - done));
        if (r <= 0) { fp->eof = 1; break; }
        done += (size_t)r;
    }
    return sz ? (done / sz) : 0;
}

int fputc(int c, FILE* fp) {
    if (fp->fd == 1 || fp->fd == 2) {
        putchar((char)c);
        return (unsigned char)c;
    }
    if (fp->is_write) {
        if (fp->write_pos >= fp->write_cap) {
            size_t new_cap = fp->write_cap == 0 ? 4096 : fp->write_cap * 2;
            if (fp->write_pos >= new_cap) new_cap = fp->write_pos + 4096;
            char* new_buf = (char*)realloc(fp->write_buf, new_cap);
            if (!new_buf) return EOF;
            fp->write_buf = new_buf;
            fp->write_cap = new_cap;
        }
        fp->write_buf[fp->write_pos++] = (char)c;
        if (fp->write_pos > fp->write_len) {
            fp->write_len = fp->write_pos;
        }
        return (unsigned char)c;
    } else {
        return EOF;
    }
}

int fputs(const char* s, FILE* fp) {
    while (*s) {
        if (fputc(*s++, fp) == EOF) return EOF;
    }
    return 0;
}

size_t fwrite(const void* ptr, size_t sz, size_t cnt, FILE* fp) {
    const char* src = (const char*)ptr;
    size_t total = sz * cnt;
    for (size_t i = 0; i < total; i++) {
        if (fputc(src[i], fp) == EOF) {
            return i / sz;
        }
    }
    return cnt;
}

int fseek(FILE* fp, long offset, int whence) {
    if (fp->is_write) {
        long new_pos = 0;
        if (whence == 0) { // SEEK_SET
            new_pos = offset;
        } else if (whence == 1) { // SEEK_CUR
            new_pos = (long)fp->write_pos + offset;
        } else if (whence == 2) { // SEEK_END
            new_pos = (long)fp->write_len + offset;
        }
        if (new_pos < 0) new_pos = 0;
        if ((size_t)new_pos > fp->write_cap) {
            size_t new_cap = (size_t)new_pos + 4096;
            char* new_buf = (char*)realloc(fp->write_buf, new_cap);
            if (!new_buf) return -1;
            memset(new_buf + fp->write_cap, 0, new_cap - fp->write_cap);
            fp->write_buf = new_buf;
            fp->write_cap = new_cap;
        }
        if ((size_t)new_pos > fp->write_len) {
            memset(fp->write_buf + fp->write_len, 0, (size_t)new_pos - fp->write_len);
            fp->write_len = (size_t)new_pos;
        }
        fp->write_pos = (size_t)new_pos;
        fp->eof = 0;
        return 0;
    } else {
        long r = fd_lseek(fp->fd, offset, whence);
        if (r < 0) return -1;
        fp->eof = 0;
        return 0;
    }
}

long ftell(FILE* fp) {
    if (fp->is_write) {
        return (long)fp->write_pos;
    } else {
        return (long)fd_lseek(fp->fd, 0, 1); // whence = 1 (SEEK_CUR)
    }
}

// ----------------------------------------------------------------------------
// Formatting Output Engine

typedef struct {
    char* buf;
    size_t size;
    size_t written;
} SnprintfCtx;

static void snprintf_putc_cb(void* ctx, char c) {
    SnprintfCtx* s = (SnprintfCtx*)ctx;
    if (s->written < s->size - 1) {
        s->buf[s->written] = c;
    }
    s->written++;
}

static void file_putc_cb(void* ctx, char c) {
    FILE* fp = (FILE*)ctx;
    fputc(c, fp);
}

void format_output(void (*putc_fn)(void* ctx, char c), void* ctx, const char* fmt, va_list ap) {
    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            putc_fn(ctx, *p++);
            continue;
        }
        p++; // skip '%'
        if (*p == '%') {
            putc_fn(ctx, '%');
            p++;
            continue;
        }
        
        int zero_pad = 0;
        if (*p == '0') {
            zero_pad = 1;
            p++;
        }
        
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }
        
        int is_long_long = 0;
        int is_long = 0;
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                is_long_long = 1;
                p++;
            } else {
                is_long = 1;
            }
        } else if (*p == 'I') {
            if (p[1] == '6' && p[2] == '4') {
                is_long_long = 1;
                p += 3;
            }
        }
        
        char spec = *p++;
        switch (spec) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                putc_fn(ctx, c);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int len = strlen(s);
                if (width > len) {
                    for (int i = 0; i < width - len; i++) putc_fn(ctx, ' ');
                }
                while (*s) putc_fn(ctx, *s++);
                break;
            }
            case 'd':
            case 'u':
            case 'x':
            case 'p': {
                u64 val;
                if (spec == 'p') {
                    val = (u64)va_arg(ap, void*);
                } else if (is_long_long) {
                    val = va_arg(ap, u64);
                } else if (is_long) {
                    val = va_arg(ap, unsigned long);
                } else {
                    val = va_arg(ap, unsigned int);
                }
                
                char num_buf[32];
                int n_idx = 0;
                int is_neg = 0;
                int base = (spec == 'x' || spec == 'p') ? 16 : 10;
                
                if (spec == 'd') {
                    i64 sval = (i64)val;
                    if (!is_long_long && !is_long) {
                        sval = (int)val;
                    }
                    if (sval < 0) {
                        is_neg = 1;
                        val = -sval;
                    } else {
                        val = sval;
                    }
                }
                
                if (val == 0) {
                    num_buf[n_idx++] = '0';
                } else {
                    while (val > 0) {
                        num_buf[n_idx++] = "0123456789abcdef"[val % base];
                        val /= base;
                    }
                }
                
                if (spec == 'p') {
                    num_buf[n_idx++] = 'x';
                    num_buf[n_idx++] = '0';
                }
                
                int total_len = n_idx + is_neg;
                if (width > total_len && !zero_pad) {
                    for (int i = 0; i < width - total_len; i++) putc_fn(ctx, ' ');
                }
                
                if (is_neg) putc_fn(ctx, '-');
                
                if (width > total_len && zero_pad) {
                    for (int i = 0; i < width - total_len; i++) putc_fn(ctx, '0');
                }
                
                while (n_idx--) {
                    putc_fn(ctx, num_buf[n_idx]);
                }
                break;
            }
            default:
                putc_fn(ctx, '%');
                if (zero_pad) putc_fn(ctx, '0');
                putc_fn(ctx, spec);
                break;
        }
    }
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    if (size == 0) return 0;
    SnprintfCtx ctx;
    ctx.buf = buf;
    ctx.size = size;
    ctx.written = 0;
    format_output(snprintf_putc_cb, &ctx, fmt, ap);
    if (ctx.written < size) {
        buf[ctx.written] = '\0';
    } else {
        buf[size - 1] = '\0';
    }
    return (int)ctx.written;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char* buf, const char* fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE* fp, const char* fmt, va_list ap) {
    format_output(file_putc_cb, fp, fmt, ap);
    return 0;
}

int fprintf(FILE* fp, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

int vprintf(const char* fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

// ----------------------------------------------------------------------------
// General Standard Library Wrappers

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    MallocHeader* blk = (MallocHeader*)((u8*)ptr - sizeof(MallocHeader));
    size_t old_size = blk->size;
    if (old_size >= size) return ptr;
    void* new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, (u32)old_size);
    free(ptr);
    return new_ptr;
}

static void qsort_swap(char* a, char* b, size_t width) {
    char tmp;
    while (width--) {
        tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

void qsort(void* base, size_t num, size_t width, int (*compare)(const void*, const void*)) {
    if (num < 2) return;
    char* arr = (char*)base;
    char* pivot = arr + (num / 2) * width;
    size_t i = 0;
    size_t j = num - 1;
    while (1) {
        while (compare(arr + i * width, pivot) < 0) i++;
        while (compare(arr + j * width, pivot) > 0) j--;
        if (i >= j) break;
        qsort_swap(arr + i * width, arr + j * width, width);
        if (pivot == arr + i * width) pivot = arr + j * width;
        else if (pivot == arr + j * width) pivot = arr + i * width;
        i++;
        j--;
    }
    qsort(base, i, width, compare);
    qsort(arr + i * width, num - i, width, compare);
}

char* strerror(int errnum) {
    return "Unknown error";
}

char* getenv(const char* name) {
    return NULL;
}

// ----------------------------------------------------------------------------
// String Helpers

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

// ----------------------------------------------------------------------------
// POSIX IO Wrappers

#define MAX_VFD 16
typedef struct {
    char* path;
    int flags;
    char* buf;
    size_t len;
    size_t cap;
    size_t pos;
    int used;
} VirtualFD;

static VirtualFD vfd_table[MAX_VFD] = {0};

int open(const char* pathname, int flags, ...) {
    int slot = -1;
    for (int i = 0; i < MAX_VFD; i++) {
        if (!vfd_table[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    
    memset(&vfd_table[slot], 0, sizeof(VirtualFD));
    vfd_table[slot].path = (char*)malloc(strlen(pathname) + 1);
    if (!vfd_table[slot].path) return -1;
    strcpy(vfd_table[slot].path, pathname);
    vfd_table[slot].flags = flags;
    vfd_table[slot].used = 1;

    // If opened for reading (O_RDONLY or O_RDWR)
    if ((flags & 0x0003) == 0 || (flags & 0x0003) == 2) {
        u64 rc = syscall1(SYS_FS_OPENFILE, (u64)(usize)pathname);
        if (rc != 0) {
            if ((flags & 0x0100) == 0) { // If O_CREAT is not set, fail
                free(vfd_table[slot].path);
                vfd_table[slot].used = 0;
                return -1;
            }
        } else {
            u32 fsize = (u32)syscall0(SYS_FS_FILESIZE);
            if (fsize > 0) {
                vfd_table[slot].buf = (char*)malloc(fsize);
                if (vfd_table[slot].buf) {
                    vfd_table[slot].cap = fsize;
                    u32 pos = 0;
                    while (pos < fsize) {
                        u32 chunk = fsize - pos;
                        if (chunk > 4096) chunk = 4096;
                        u64 n = syscall2(SYS_FS_READFILE, (u64)(usize)vfd_table[slot].buf + pos, (u64)chunk);
                        if (n == 0 || n == (u64)-1) break;
                        pos += (u32)n;
                    }
                    vfd_table[slot].len = pos;
                }
            }
            syscall0(SYS_FS_CLOSEFILE);
        }
    }

    return slot + 1000;
}

int close(int fd) {
    if (fd >= 1000 && fd < 1000 + MAX_VFD) {
        int slot = fd - 1000;
        if (vfd_table[slot].used) {
            if (vfd_table[slot].path) {
                // Only write to disk if opened for writing (O_WRONLY=1, O_RDWR=2)
                if ((vfd_table[slot].flags & 0x0003) == 1 || (vfd_table[slot].flags & 0x0003) == 2) {
                    if (vfd_table[slot].buf && vfd_table[slot].len > 0) {
                        syscall3(SYS_FS_WRITEFILE, (u64)(usize)vfd_table[slot].path, (u64)(usize)vfd_table[slot].buf, (u64)vfd_table[slot].len);
                    } else {
                        syscall3(SYS_FS_WRITEFILE, (u64)(usize)vfd_table[slot].path, (u64)(usize)"", 0);
                    }
                }
                free(vfd_table[slot].path);
            }
            if (vfd_table[slot].buf) {
                free(vfd_table[slot].buf);
            }
            vfd_table[slot].used = 0;
            return 0;
        }
        return -1;
    }
    return fd_close(fd);
}

int read(int fd, void* buf, unsigned int count) {
    if (fd == 0) {
        char* dst = (char*)buf;
        for (unsigned int i = 0; i < count; i++) {
            dst[i] = getchar();
        }
        return count;
    }
    if (fd >= 1000 && fd < 1000 + MAX_VFD) {
        int slot = fd - 1000;
        if (!vfd_table[slot].used) return -1;
        if (vfd_table[slot].pos >= vfd_table[slot].len) return 0;
        size_t rem = vfd_table[slot].len - vfd_table[slot].pos;
        size_t cnt = count < rem ? count : rem;
        memcpy(buf, vfd_table[slot].buf + vfd_table[slot].pos, cnt);
        vfd_table[slot].pos += cnt;
        return (int)cnt;
    }
    return fd_read(fd, buf, count);
}

int write(int fd, const void* buf, unsigned int count) {
    if (fd == 1 || fd == 2) {
        const char* s = (const char*)buf;
        for (unsigned int i = 0; i < count; i++) {
            putchar(s[i]);
        }
        return count;
    }
    if (fd >= 1000 && fd < 1000 + MAX_VFD) {
        int slot = fd - 1000;
        if (!vfd_table[slot].used) return -1;
        VirtualFD* v = &vfd_table[slot];
        if (v->pos + count > v->cap) {
            size_t new_cap = v->cap == 0 ? 4096 : v->cap * 2;
            if (v->pos + count > new_cap) new_cap = v->pos + count + 4096;
            char* new_buf = (char*)realloc(v->buf, new_cap);
            if (!new_buf) return -1;
            v->buf = new_buf;
            v->cap = new_cap;
        }
        memcpy(v->buf + v->pos, buf, count);
        v->pos += count;
        if (v->pos > v->len) {
            v->len = v->pos;
        }
        return count;
    }
    return -1;
}

int lseek(int fd, long offset, int whence) {
    if (fd >= 1000 && fd < 1000 + MAX_VFD) {
        int slot = fd - 1000;
        if (!vfd_table[slot].used) return -1;
        VirtualFD* v = &vfd_table[slot];
        size_t new_pos = v->pos;
        if (whence == 0) {
            new_pos = offset;
        } else if (whence == 1) {
            new_pos = v->pos + offset;
        } else if (whence == 2) {
            new_pos = v->len + offset;
        }
        v->pos = new_pos;
        return (int)v->pos;
    }
    return fd_lseek(fd, offset, whence);
}

int unlink(const char* pathname) {
    return syscall1(SYS_FS_DELETE, (u64)(usize)pathname) == 0 ? 0 : -1;
}

char* getcwd(char* buf, size_t size) {
    u64 rc = syscall2(SYS_GETCWD, (u64)(usize)buf, (u64)size);
    if (rc == 0) return buf;
    return NULL;
}

int chmod(const char* path, int mode) {
    return 0;
}

int gettimeofday(struct timeval* tv, void* tz) {
    if (tv) {
        u64 ticks = syscall2(SYS_SYSTEM, SYS_SYS_TICKS, 0);
        tv->tv_sec = ticks / 100;
        tv->tv_usec = (ticks % 100) * 10000;
    }
    return 0;
}

time_t time(time_t* t) {
    u64 ticks = syscall2(SYS_SYSTEM, SYS_SYS_TICKS, 0);
    time_t sec = ticks / 100;
    if (t) *t = sec;
    return sec;
}

// MSVC float helper
int _fltused = 0x9876;

void exit(int code) {
    syscall1(2, (u64)code); // SYS_EXIT = 2
    for (;;);
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int fflush(FILE* stream) {
    return 0;
}

FILE* fdopen(int fd, const char* mode) {
    int is_write = 0;
    if (strchr(mode, 'w') || strchr(mode, 'a')) {
        is_write = 1;
    }
    if (fd >= 1000 && fd < 1000 + MAX_VFD) {
        int slot = fd - 1000;
        FILE* fp = (FILE*)malloc(sizeof(FILE));
        if (!fp) return NULL;
        memset(fp, 0, sizeof(FILE));
        fp->fd = fd;
        fp->is_write = 1;
        fp->path = (char*)malloc(strlen(vfd_table[slot].path) + 1);
        if (fp->path) strcpy(fp->path, vfd_table[slot].path);
        fp->eof = 0;
        fp->err = 0;
        return fp;
    }
    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp) return NULL;
    memset(fp, 0, sizeof(FILE));
    fp->fd = fd;
    fp->is_write = is_write;
    fp->eof = 0;
    fp->err = 0;
    return fp;
}

int remove(const char* pathname) {
    return unlink(pathname);
}

int system(const char* command) {
    return -1;
}

int execvp(const char* file, char* const argv[]) {
    return -1;
}

char* strpbrk(const char* s1, const char* s2) {
    while (*s1) {
        const char* p = s2;
        while (*p) {
            if (*s1 == *p) {
                return (char*)s1;
            }
            p++;
        }
        s1++;
    }
    return NULL;
}

double strtod(const char* nptr, char** endptr) {
    const char* p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    double sign = 1.0;
    if (*p == '-') {
        sign = -1.0;
        p++;
    } else if (*p == '+') {
        p++;
    }
    double val = 0.0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10.0 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double dec = 0.1;
        while (*p >= '0' && *p <= '9') {
            val += (*p - '0') * dec;
            dec *= 0.1;
            p++;
        }
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        int exp_sign = 1;
        if (*p == '-') {
            exp_sign = -1;
            p++;
        } else if (*p == '+') {
            p++;
        }
        int exp_val = 0;
        while (*p >= '0' && *p <= '9') {
            exp_val = exp_val * 10 + (*p - '0');
            p++;
        }
        double exp_mult = 1.0;
        for (int i = 0; i < exp_val; i++) {
            exp_mult *= 10.0;
        }
        if (exp_sign > 0) {
            val *= exp_mult;
        } else {
            val /= exp_mult;
        }
    }
    if (endptr) {
        *endptr = (char*)p;
    }
    return val * sign;
}

float strtof(const char* nptr, char** endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char* nptr, char** endptr) {
    return (long double)strtod(nptr, endptr);
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (base == 0) {
        if (*p == '0') {
            p++;
            if (*p == 'x' || *p == 'X') {
                base = 16;
                p++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
    }
    unsigned long long val = 0;
    for (;;) {
        int digit = -1;
        char c = *p;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        if (digit < 0 || digit >= base) break;
        val = val * base + digit;
        p++;
    }
    if (endptr) {
        *endptr = (char*)p;
    }
    return val * sign;
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    return (unsigned long)strtoull(nptr, endptr, base);
}

long strtol(const char* nptr, char** endptr, int base) {
    return (long)strtoull(nptr, endptr, base);
}

long double ldexpl(long double x, int exp) {
    long double mult = 1.0;
    if (exp > 0) {
        for (int i = 0; i < exp; i++) mult *= 2.0;
        return x * mult;
    } else if (exp < 0) {
        for (int i = 0; i < -exp; i++) mult *= 2.0;
        return x / mult;
    }
    return x;
}

static struct tm local_tm;
struct tm* localtime(const time_t* timep) {
    time_t t = timep ? *timep : 0;
    local_tm.tm_sec = (int)(t % 60);
    t /= 60;
    local_tm.tm_min = (int)(t % 60);
    t /= 60;
    local_tm.tm_hour = (int)(t % 24);
    t /= 24;
    int year = 1970;
    for (;;) {
        int days_in_year = 365;
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            days_in_year = 366;
        }
        if (t < days_in_year) break;
        t -= days_in_year;
        year++;
    }
    local_tm.tm_year = year - 1900;
    local_tm.tm_yday = (int)t;
    int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    int mon = 0;
    while (t >= days_in_month[mon]) {
        t -= days_in_month[mon];
        mon++;
    }
    local_tm.tm_mon = mon;
    local_tm.tm_mday = (int)(t + 1);
    return &local_tm;
}

char* realpath(const char* path, char* resolved_path) {
    if (!resolved_path) {
        resolved_path = (char*)malloc(260);
    }
    if (!resolved_path) return NULL;
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':')) {
        strcpy(resolved_path, path);
    } else {
        getcwd(resolved_path, 260);
        int len = strlen(resolved_path);
        if (len > 0 && resolved_path[len-1] != '/' && resolved_path[len-1] != '\\') {
            strcat(resolved_path, "/");
        }
        strcat(resolved_path, path);
    }
    return resolved_path;
}

extern int main(int argc, char** argv);

void tcc_main_entry(const char* args, const char* cwd, i32 argc_dummy) {
    char* argv[128] = {0};
    int ac = 0;
    
    argv[ac++] = "tcc.exe";
    
    if (args && *args) {
        char* argbuf = (char*)malloc(strlen(args) + 1);
        if (argbuf) {
            strcpy(argbuf, args);
            ac += parse_args(argbuf, argv + 1, 127);
        }
    }
    
    int ret = main(ac, argv);
    exit(ret);
}