#include "micront_crt.h"

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long i64;
typedef int i32;

#define PAGE_SIZE 4096
#define SYS_MMAP        4
#define SYS_PUTS       10
#define SYS_PUTCHAR    41
#define SYS_SLEEP      39
#define SYS_SYSTEM     22
#define SYS_EXIT        2
#define SYS_OPEN       45
#define SYS_READ       46
#define SYS_LSEEK      47
#define SYS_CLOSEFD    48
#define SYS_FS_WRITEFILE 35
#define SYS_FS_DELETE    36
#define SYS_FS_MKDIR     37

u64 syscall0(u64 num);
u64 syscall1(u64 num, u64 a1);
u64 syscall2(u64 num, u64 a1, u64 a2);
u64 syscall3(u64 num, u64 a1, u64 a2, u64 a3);

static void do_puts(const char* s) {
    syscall1(SYS_PUTS, (u64)(size_t)s);
}

static void do_putchar(char c) {
    syscall1(SYS_PUTCHAR, (u64)(unsigned char)c);
}

static void do_exit(int code) {
    syscall1(SYS_EXIT, (u64)code);
    for (;;) ;
}

static u64 do_get_ticks(void) {
    return syscall2(SYS_SYSTEM, 2, 0);
}

static void do_sleep(u64 ms) {
    syscall1(SYS_SLEEP, ms);
}

static u64 do_mmap(u64 size) {
    return syscall1(SYS_MMAP, size);
}

typedef struct _FILEImpl {
    int fd;
    int eof;
    int err;
    int write_mode;
    char* write_path;
    char* write_buf;
    size_t write_pos;
    size_t write_cap;
} _FILEImpl;

static _FILEImpl _stdin_impl = {0, 0, 0, 0, NULL, NULL, 0, 0};
static _FILEImpl _stdout_impl = {1, 0, 0, 0, NULL, NULL, 0, 0};
static _FILEImpl _stderr_impl = {2, 0, 0, 0, NULL, NULL, 0, 0};

FILE* stdin = (FILE*)&_stdin_impl;
FILE* stdout = (FILE*)&_stdout_impl;
FILE* stderr = (FILE*)&_stderr_impl;

FILE* fopen(const char* path, const char* mode) {
    int is_write = 0;
    if (mode) {
        for (const char* m = mode; *m; m++) {
            if (*m == 'w' || *m == 'a' || *m == '+') is_write = 1;
        }
    }
    if (is_write) {
        _FILEImpl* fp = (_FILEImpl*)malloc(sizeof(_FILEImpl));
        if (!fp) return NULL;
        fp->fd = -1;
        fp->eof = 0;
        fp->err = 0;
        fp->write_mode = 1;
        size_t plen = strlen(path) + 1;
        fp->write_path = (char*)malloc(plen);
        if (!fp->write_path) { free(fp); return NULL; }
        strcpy(fp->write_path, path);
        fp->write_cap = 65536;
        fp->write_buf = (char*)malloc(fp->write_cap);
        if (!fp->write_buf) { free(fp->write_path); free(fp); return NULL; }
        fp->write_pos = 0;
        return (FILE*)fp;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    _FILEImpl* fp = (_FILEImpl*)malloc(sizeof(_FILEImpl));
    if (!fp) { close(fd); return NULL; }
    fp->fd = fd; fp->eof = 0; fp->err = 0;
    fp->write_mode = 0;
    fp->write_path = NULL;
    fp->write_buf = NULL;
    fp->write_pos = 0;
    fp->write_cap = 0;
    return (FILE*)fp;
}

int fclose(FILE* fp) {
    if (!fp) return EOF;
    _FILEImpl* impl = (_FILEImpl*)fp;
    if (impl->write_mode && impl->write_buf) {
        if (impl->write_pos > 0) {
            syscall3(SYS_FS_WRITEFILE, (u64)(size_t)impl->write_path, (u64)(size_t)impl->write_buf, (u64)impl->write_pos);
        }
        free(impl->write_buf);
        free(impl->write_path);
    } else {
        if (impl->fd >= 0) close(impl->fd);
    }
    free(impl);
    return 0;
}

int feof(FILE* fp) { return fp ? ((_FILEImpl*)fp)->eof : 0; }
int ferror(FILE* fp) { return fp ? ((_FILEImpl*)fp)->err : 0; }
int fflush(FILE* fp) { return 0; }

int fgetc(FILE* fp) {
    unsigned char c;
    int r = read(((_FILEImpl*)fp)->fd, &c, 1);
    if (r <= 0) { ((_FILEImpl*)fp)->eof = 1; return EOF; }
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

int fputc(int c, FILE* fp) {
    char ch = (char)c;
    if (fp == stdout || fp == stderr) { do_putchar(ch); return c; }
    int r = write(((_FILEImpl*)fp)->fd, &ch, 1);
    return r > 0 ? c : EOF;
}

size_t fread(void* buf, size_t sz, size_t cnt, FILE* fp) {
    u8* p = (u8*)buf;
    size_t total = sz * cnt;
    size_t done = 0;
    while (done < total) {
        int r = read(((_FILEImpl*)fp)->fd, p + done, (unsigned int)(total - done));
        if (r <= 0) { ((_FILEImpl*)fp)->eof = 1; break; }
        done += (size_t)r;
    }
    return done / sz;
}

size_t fwrite(const void* buf, size_t sz, size_t cnt, FILE* fp) {
    if (fp == stdout || fp == stderr) {
        const char* s = (const char*)buf;
        size_t total = sz * cnt;
        for (size_t i = 0; i < total; i++) do_putchar(s[i]);
        return cnt;
    }
    _FILEImpl* impl = (_FILEImpl*)fp;
    if (impl->write_mode && impl->write_buf) {
        size_t total = sz * cnt;
        while (impl->write_pos + total > impl->write_cap) {
            size_t newcap = impl->write_cap * 2;
            char* newbuf = (char*)realloc(impl->write_buf, newcap);
            if (!newbuf) return 0;
            impl->write_buf = newbuf;
            impl->write_cap = newcap;
        }
        memcpy(impl->write_buf + impl->write_pos, buf, total);
        impl->write_pos += total;
        return cnt;
    }
    int r = write(((_FILEImpl*)fp)->fd, buf, (unsigned int)(sz * cnt));
    return r > 0 ? (size_t)r / sz : 0;
}

int fseek(FILE* fp, long offset, int whence) {
    long r = lseek(((_FILEImpl*)fp)->fd, offset, whence);
    if (r < 0) return -1;
    ((_FILEImpl*)fp)->eof = 0;
    return 0;
}

long ftell(FILE* fp) { 
    _FILEImpl* impl = (_FILEImpl*)fp;
    if (impl && impl->write_mode) return (long)impl->write_pos;
    return lseek(((_FILEImpl*)fp)->fd, 0, SEEK_CUR); 
}
void rewind(FILE* fp) { fseek(fp, 0, SEEK_SET); }

int vfprintf(FILE* fp, const char* fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    if (fp == stdout || fp == stderr) { do_puts(buf); return (int)strlen(buf); }
    return (int)fwrite(buf, 1, strlen(buf), fp);
}

int fscanf(FILE* fp, const char* fmt, ...) { return 0; }
void setbuf(FILE* fp, char* buf) {}
void clearerr(FILE* fp) { if (fp) { ((_FILEImpl*)fp)->eof = 0; ((_FILEImpl*)fp)->err = 0; } }
int ungetc(int c, FILE* fp) { return EOF; }

#define MALLOC_ALIGN 16
#define MALLOC_MIN_BLOCK 64

typedef struct MallocHeader {
    size_t size;
    int used;
    struct MallocHeader* next;
} MallocHeader;

static MallocHeader* malloc_head = NULL;

static void* malloc_from_mmap(size_t size) {
    size_t total = sizeof(MallocHeader) + size;
    total = (total + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);
    u64 ptr = do_mmap(total);
    if (!ptr) return NULL;
    MallocHeader* blk = (MallocHeader*)ptr;
    size_t excess = total - sizeof(MallocHeader) - size;
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
    } else {
        blk->size = total - sizeof(MallocHeader);
        blk->used = 1;
        blk->next = malloc_head;
        malloc_head = blk;
    }
    return (void*)((u8*)blk + sizeof(MallocHeader));
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + MALLOC_ALIGN - 1) & ~((size_t)MALLOC_ALIGN - 1);
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

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    MallocHeader* blk = (MallocHeader*)((u8*)ptr - sizeof(MallocHeader));
    if (blk->size >= size) return ptr;
    void* np = malloc(size);
    if (!np) return NULL;
    size_t copy = blk->size < size ? blk->size : size;
    memcpy(np, ptr, copy);
    free(ptr);
    return np;
}

void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* memcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    u8* d = (u8*)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const u8* pa = (const u8*)a; const u8* pb = (const u8*)b;
    while (n--) { if (*pa != *pb) return *pa - *pb; pa++; pb++; }
    return 0;
}

size_t strlen(const char* s) { size_t n = 0; while (*s++) n++; return n; }

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

char* strcpy(char* dst, const char* src) { char* d = dst; while ((*d++ = *src++)); return dst; }

char* strncpy(char* dst, const char* src, size_t n) {
    char* d = dst;
    while (n > 0 && *src) { *d++ = *src++; n--; }
    while (n > 0) { *d++ = 0; n--; }
    return dst;
}

char* strcat(char* dst, const char* src) { char* d = dst + strlen(dst); while ((*d++ = *src++)); return dst; }

char* strncat(char* dst, const char* src, size_t n) {
    char* d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = 0; return dst;
}

char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return c == 0 ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    char* last = NULL;
    while (*s) { if (*s == (char)c) last = (char*)s; s++; }
    return c == 0 ? (char*)s : last;
}

char* strstr(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char*)haystack;
    while (*haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
            return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static char* strtok_next;
char* strtok(char* str, const char* delim) {
    if (!str) str = strtok_next;
    if (!str) return NULL;
    str += strspn(str, delim);
    if (!*str) { strtok_next = NULL; return NULL; }
    char* end = str + strcspn(str, delim);
    if (*end) { *end = 0; strtok_next = end + 1; } else strtok_next = NULL;
    return str;
}

size_t strspn(const char* s, const char* accept) {
    size_t n = 0; while (*s && strchr(accept, *s)) { s++; n++; } return n;
}

size_t strcspn(const char* s, const char* reject) {
    size_t n = 0; while (*s && !strchr(reject, *s)) { s++; n++; } return n;
}

char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* d = (char*)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && *b) { int ca = tolower((unsigned char)*a); int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb; a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *b) { int ca = tolower((unsigned char)*a); int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb; a++; b++; }
    if (n == (size_t)-1) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 0x20 && c <= 0x7e; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && !isspace(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c) { return (c >= 0 && c <= 0x1f) || c == 0x7f; }
int isgraph(int c) { return c >= 0x21 && c <= 0x7e; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int atoi(const char* s) { return (int)strtol(s, NULL, 10); }
long atol(const char* s) { return strtol(s, NULL, 10); }

long strtol(const char* s, char** end, int base) {
    while (isspace(*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') base = 8; else base = 10;
    }
    long val = 0;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        val = val * base + digit; s++;
    }
    if (end) *end = (char*)s;
    return neg ? -val : val;
}

int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }

static void format_int(char* buf, long val, int base, int is_unsigned, int width, int zeropad) {
    char tmp[22]; int i = 0; int neg = 0; unsigned long uval;
    if (!is_unsigned && val < 0 && base == 10) { neg = 1; uval = (unsigned long)(-val); }
    else uval = (unsigned long)val;
    if (uval == 0) tmp[i++] = '0';
    else while (uval > 0) { tmp[i++] = "0123456789abcdef"[uval % base]; uval /= base; }
    int len = i; if (neg) len++;
    if (width > len && zeropad) for (int p = 0; p < width - len; p++) *buf++ = '0';
    if (neg) *buf++ = '-';
    while (i--) *buf++ = tmp[i];
    *buf = 0;
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    char* p = buf; char* end = buf + size - 1;
    if (size == 0) return 0;
    while (*fmt && p < end) {
        if (*fmt != '%') { *p++ = *fmt++; continue; }
        fmt++;
        int width = 0, zeropad = 0;
        if (*fmt == '0') { zeropad = 1; fmt++; }
        while (*fmt >= '1' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') { fmt++; width = 0; while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; } zeropad = 1; }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == 'l') { is_long = 2; fmt++; }
        switch (*fmt) {
        case '%': *p++ = '%'; break;
        case 'c': { char c = (char)va_arg(ap, int); *p++ = c; break; }
        case 's': { const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)"; while (*s && p < end) *p++ = *s++; break; }
        case 'd': case 'i': {
            long v = is_long >= 2 ? va_arg(ap, long long) : is_long ? va_arg(ap, long) : va_arg(ap, int);
            char num[22]; format_int(num, v, 10, 0, width, zeropad);
            char* np = num; while (*np && p < end) *p++ = *np++; break; }
        case 'u': {
            unsigned long v = is_long >= 2 ? va_arg(ap, unsigned long long) : is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            char num[22]; format_int(num, (long)v, 10, 1, width, zeropad);
            char* np = num; while (*np && p < end) *p++ = *np++; break; }
        case 'x': case 'X': {
            unsigned long v = is_long >= 2 ? va_arg(ap, unsigned long long) : is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            char num[22]; format_int(num, (long)v, 16, 1, width, zeropad);
            char* np = num; while (*np && p < end) *p++ = *np++; break; }
        case 'p': {
            unsigned long v = (unsigned long)(size_t)va_arg(ap, void*);
            *p++ = '0'; if (p < end) *p++ = 'x';
            char num[22]; format_int(num, (long)v, 16, 1, width, zeropad);
            char* np = num; while (*np && p < end) *p++ = *np++; break; }
        default: *p++ = '%'; if (p < end) *p++ = *fmt; break;
        }
        fmt++;
    }
    *p = 0;
    return (int)(p - buf);
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, size, fmt, ap); va_end(ap); return r;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, 65536, fmt, ap); va_end(ap); return r;
}

static char g_printf_buf[1024];

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_printf_buf, sizeof(g_printf_buf), fmt, ap); va_end(ap);
    do_puts(g_printf_buf); return (int)strlen(g_printf_buf);
}

int fprintf(void* fp, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_printf_buf, sizeof(g_printf_buf), fmt, ap); va_end(ap);
    do_puts(g_printf_buf); return (int)strlen(g_printf_buf);
}

int fputs(const char* s, void* fp) { do_puts(s); return 0; }
int puts(const char* s) { do_puts(s); do_putchar('\n'); return 0; }
int putchar(int c) { do_putchar((char)c); return c; }

int open(const char* path, int flags, ...) { 
    return (int)syscall1(SYS_OPEN, (u64)(size_t)path); 
}
int read(int fd, void* buf, unsigned int count) { return (int)syscall3(SYS_READ, (u64)fd, (u64)(size_t)buf, (u64)count); }

int write(int fd, const void* buf, unsigned int count) {
    if (fd == 1 || fd == 2) { const char* s = (const char*)buf; for (unsigned int i = 0; i < count; i++) do_putchar(s[i]); return (int)count; }
    return -1;
}

int close(int fd) { return (int)syscall1(SYS_CLOSEFD, (u64)fd); }
long lseek(int fd, long offset, int whence) { return (long)syscall3(SYS_LSEEK, (u64)fd, (u64)offset, (u64)whence); }

int access(const char* path, int mode) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    close(fd); return 0;
}

static void swap_bytes(void* a, void* b, size_t size) {
    u8* pa = (u8*)a; u8* pb = (u8*)b;
    while (size--) { u8 t = *pa; *pa++ = *pb; *pb++ = t; }
}

void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*)) {
    if (nmemb <= 1) return;
    u8* arr = (u8*)base;
    u8* pivot = arr + (nmemb - 1) * size;
    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (cmp(arr + j * size, pivot) < 0) { swap_bytes(arr + i * size, arr + j * size, size); i++; }
    }
    swap_bytes(arr + i * size, pivot, size);
    qsort(arr, i, size, cmp);
    qsort(arr + (i + 1) * size, nmemb - i - 1, size, cmp);
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*)) {
    const u8* arr = (const u8*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, arr + mid * size);
        if (r == 0) return (void*)(arr + mid * size);
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

static unsigned long g_rand_seed = 1;
int rand(void) { g_rand_seed = g_rand_seed * 1103515245 + 12345; return (int)((g_rand_seed / 65536) % 32768); }
void srand(unsigned int seed) { g_rand_seed = seed; }

unsigned long time(void* t) {
    u64 ticks = do_get_ticks();
    unsigned long secs = (unsigned long)(ticks / 100);
    if (t) *(unsigned long*)t = secs;
    return secs;
}

void exit(int code) { do_exit(code); }
void abort(void) { do_exit(99); }
char* getenv(const char* name) { return NULL; }
int atexit(void (*fn)(void)) { return 0; }
int errno = 0;
int remove(const char* path) {
    int r = (int)syscall1(SYS_FS_DELETE, (u64)(size_t)path);
    return r;
}

typedef void (*sighandler_t)(int);
static sighandler_t g_signal_handlers[16] = {0};
sighandler_t signal(int sig, sighandler_t handler) {
    if (sig < 0 || sig >= 16) return NULL;
    sighandler_t old = g_signal_handlers[sig]; g_signal_handlers[sig] = handler; return old;
}
int raise(int sig) {
    if (sig < 0 || sig >= 16 || !g_signal_handlers[sig]) return -1;
    g_signal_handlers[sig](sig); return 0;
}

double fabs(double x) {
    union { double d; unsigned long long u; } u; u.d = x; u.u &= 0x7FFFFFFFFFFFFFFFULL; return u.d;
}
double ceil(double x) { long i = (long)x; return (x > 0 && x != (double)i) ? (double)(i + 1) : (double)i; }
double floor(double x) { long i = (long)x; return (x < 0 && x != (double)i) ? (double)(i - 1) : (double)i; }

int __cdecl _fltused = 0;

int sscanf(const char* str, const char* fmt, ...) { return 0; }
double atof(const char* s) { return strtod(s, NULL); }
int rename(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) { return -1; }
    FILE* f = fopen(oldpath, "rb");
    if (!f) { return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    long total = 0;
    while (total < sz) {
        int r = (int)fread(buf + total, 1, (size_t)(sz - total), f);
        if (r <= 0) break;
        total += r;
    }
    fclose(f);
    if (total <= 0) { free(buf); return -1; }
    int wr = (int)syscall3(SYS_FS_WRITEFILE, (u64)(size_t)newpath, (u64)(size_t)buf, (u64)total);
    free(buf);
    if (wr != 0) { return -1; }
    syscall1(SYS_FS_DELETE, (u64)(size_t)oldpath);
    return 0;
}
int mkdir(const char* path, int mode) { 
    return (int)syscall1(SYS_FS_MKDIR, (u64)(size_t)path);
}
int system(const char* cmd) { return -1; }

double strtod(const char* s, char** end) {
    while (isspace(*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    double val = 0.0;
    while (isdigit(*s)) { val = val * 10.0 + (*s - '0'); s++; }
    if (*s == '.') {
        s++; double frac = 0.1;
        while (isdigit(*s)) { val += frac * (*s - '0'); frac *= 0.1; s++; }
    }
    if (*s == 'e' || *s == 'E') {
        s++; int eneg = 0; int exp = 0;
        if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
        while (isdigit(*s)) { exp = exp * 10 + (*s - '0'); s++; }
        double mult = 1.0; for (int i = 0; i < exp; i++) mult *= 10.0;
        if (eneg) val /= mult; else val *= mult;
    }
    if (end) *end = (char*)s;
    return neg ? -val : val;
}
