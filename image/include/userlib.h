#ifndef _USERLIB_H_
#define _USERLIB_H_

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long i64;
typedef int i32;
typedef short i16;
typedef signed char i8;
typedef u64 usize;

#define SYS_WRITE       1
#define SYS_EXIT        2
#define SYS_GETPID      3
#define SYS_MMAP        4
#define SYS_PUTS       10
#define SYS_GETCHAR    11
#define SYS_CLEAR      12
#define SYS_SYSTEM     22
#define SYS_SETCOLOR   23
#define SYS_GETCWD     27
#define SYS_FS_OPENDIR  28
#define SYS_FS_READDIR  29
#define SYS_FS_CLOSEDIR 30
#define SYS_FS_OPENFILE 31
#define SYS_FS_READFILE 32
#define SYS_FS_FILESIZE 33
#define SYS_FS_CLOSEFILE 34
#define SYS_FS_WRITEFILE 35
#define SYS_FS_DELETE    36
#define SYS_FS_MKDIR     37
#define SYS_EXEC          38
#define SYS_SLEEP         39
#define SYS_CHDIR         40
#define SYS_PUTCHAR       41
#define SYS_WAITPID       42
#define SYS_FB_MAP        43
#define SYS_GETKEY        44
#define SYS_OPEN          45
#define SYS_READ          46
#define SYS_LSEEK         47
#define SYS_CLOSEFD       48

#define SYS_AUDIO_INIT    49
#define SYS_AUDIO_PLAY    50
#define SYS_AUDIO_STOP    51

#define SYS_NET_OPEN      52
#define SYS_NET_CONNECT   53
#define SYS_NET_SEND      54
#define SYS_NET_RECV      55
#define SYS_NET_CLOSE     56

#define SYS_SYS_REBOOT      0
#define SYS_SYS_HALT        1
#define SYS_SYS_TICKS       2
#define SYS_SYS_MEMINFO     3
#define SYS_SYS_PS          4

#define DIR_MAX_NAME 256

typedef struct {
    char name[DIR_MAX_NAME];
    u32  file_size;
    i32  is_directory;
} DirEntry;

typedef struct {
    u64 fb_virt;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
} FbInfo;

typedef struct {
    u32 scancode;
    u32 pressed;
} KeyEvent;

#define FB_BLACK   0x00000000
#define FB_WHITE   0x00FFFFFF
#define FB_RED     0x00FF0000
#define FB_GREEN   0x0000FF00
#define FB_BLUE    0x000000FF
#define FB_CYAN    0x0000FFFF
#define FB_YELLOW  0x00FFFF00
#define FB_MAGENTA 0x00FF00FF
#define FB_GRAY    0x00808080

#define PS_INFO_MAX 32

typedef struct {
    char name[64];
    u64  pid;
    i32  state;
    u32  thread_count;
} PsInfoEntry;

u64 syscall0(u64 num);
u64 syscall1(u64 num, u64 a1);
u64 syscall2(u64 num, u64 a1, u64 a2);
u64 syscall3(u64 num, u64 a1, u64 a2, u64 a3);
u64 syscall4(u64 num, u64 a1, u64 a2, u64 a3, u64 a4);

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long long size_t;
#endif

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef unsigned long long time_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

struct timeval {
    long tv_sec;
    long tv_usec;
};

#ifndef _TM_DEFINED
#define _TM_DEFINED
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};
#endif

size_t strlen(const char* s);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, size_t n);
char*  strcat(char* dst, const char* src);
i32    strcmp(const char* a, const char* b);
i32    strncmp(const char* a, const char* b, size_t n);
void*  memset(void* dst, i32 val, size_t count);
void*  memcpy(void* dst, const void* src, size_t count);
void*  memmove(void* dst, const void* src, size_t count);
i32    memcmp(const void* a, const void* b, size_t count);

void  print(const char* s);
void  puts(const char* s);
void  putchar(char c);
void  clear(void);
void  setcolor(i32 fg, i32 bg);
char  getchar(void);
void  exit(i32 code);
void  chdir(const char* path);
u64   waitpid(u64 pid);
i32   fb_map(FbInfo* info);
i32   getkey(KeyEvent* ev);
i32   fd_open(const char* path);
i32   fd_read(i32 fd, void* buf, u32 count);
i32   fd_lseek(i32 fd, i64 offset, i32 whence);
i32   fd_close(i32 fd);

i32   audio_init(void);
i32   audio_play(const void* buf, u32 size);
i32   audio_stop(void);

i32   net_socket(void);
i32   net_connect(i32 sock, u32 ip, u16 port);
i32   net_send(i32 sock, const void* buf, u16 len);
i32   net_recv(i32 sock, void* buf, u16 len, u32 timeout_ms);
i32   net_close(i32 sock);

void  itoa(i64 val, char* buf, i32 base);
void  utoa(u64 val, char* buf, i32 base);
void  printf(const char* fmt, ...);

i32 parse_args(const char* args, char** argv, i32 max);

// Standard compatibility layer extensions
typedef struct _FILE FILE;
extern FILE* stdout;
extern FILE* stderr;
extern FILE* stdin;

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* fp);
int feof(FILE* fp);
int ferror(FILE* fp);
int fgetc(FILE* fp);
char* fgets(char* buf, int n, FILE* fp);
size_t fread(void* buf, size_t sz, size_t cnt, FILE* fp);
size_t fwrite(const void* ptr, size_t sz, size_t cnt, FILE* fp);
int fseek(FILE* fp, long offset, int whence);
long ftell(FILE* fp);
int fputc(int c, FILE* fp);
int fputs(const char* s, FILE* fp);

#include <stdarg.h>
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsprintf(char* buf, const char* fmt, va_list ap);
int sprintf(char* buf, const char* fmt, ...);
int vfprintf(FILE* fp, const char* fmt, va_list ap);
int fprintf(FILE* fp, const char* fmt, ...);
int vprintf(const char* fmt, va_list ap);

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t count, size_t size);
void* realloc(void* ptr, size_t size);

void qsort(void* base, size_t num, size_t width, int (*compare)(const void*, const void*));
char* strerror(int errnum);
char* getenv(const char* name);

char* strstr(const char* haystack, const char* needle);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);

int gettimeofday(struct timeval* tv, void* tz);
time_t time(time_t* t);

void exit(int code);
int strcasecmp(const char* s1, const char* s2);
int fflush(FILE* stream);
FILE* fdopen(int fd, const char* mode);
int remove(const char* pathname);
int system(const char* command);
int execvp(const char* file, char* const argv[]);
char* strpbrk(const char* s1, const char* s2);
double strtod(const char* nptr, char** endptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
long strtol(const char* nptr, char** endptr, int base);
long double ldexpl(long double x, int exp);
struct tm* localtime(const time_t* timep);
char* realpath(const char* path, char* resolved_path);

#endif