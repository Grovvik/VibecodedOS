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

i32 fd_open(const char* path) {
    return (i32)syscall1(SYS_OPEN, (u64)(usize)path);
}

i32 fd_read(i32 fd, void* buf, u32 count) {
    return (i32)syscall3(SYS_READ, (u64)fd, (u64)(usize)buf, (u64)count);
}

i32 fd_lseek(i32 fd, i64 offset, i32 whence) {
    return (i32)syscall3(SYS_LSEEK, (u64)fd, (u64)offset, (u64)whence);
}

i32 fd_close(i32 fd) {
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

// ============================================================================
// ����� 5: ��������������� �������� ����-����� (��������� FILE)
// ============================================================================

typedef struct _FILE {
    int fd;
    int eof;
    int err;
} FILE;

FILE* fopen(const char* path, const char* mode) {
    // ����������: � ����� fd_open ���� ��� ��������� ������ mode. 
    // ���� ���� ������ �� ���������, ���� ����� ����� �������� �������.
    int fd = fd_open(path);
    if (fd < 0) return NULL;

    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp) {
        fd_close(fd);
        return NULL;
    }
    fp->fd = fd;
    fp->eof = 0;
    fp->err = 0;
    return fp;
}

int fclose(FILE* fp) {
    if (!fp) return EOF;
    fd_close(fp->fd);
    free(fp);
    return 0;
}

int feof(FILE* fp) { return fp ? fp->eof : 0; }
int ferror(FILE* fp) { return fp ? fp->err : 0; }

int fgetc(FILE* fp) {
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

usize fread(void* buf, usize sz, usize cnt, FILE* fp) {
    u8* p = (u8*)buf;
    usize total = sz * cnt;
    usize done = 0;
    while (done < total) {
        int r = fd_read(fp->fd, p + done, (u32)(total - done));
        if (r <= 0) { fp->eof = 1; break; }
        done += (usize)r;
    }
    return sz ? (done / sz) : 0;
}

int fseek(FILE* fp, long offset, int whence) {
    long r = fd_lseek(fp->fd, offset, whence);
    if (r < 0) return -1;
    fp->eof = 0;
    return 0;
}