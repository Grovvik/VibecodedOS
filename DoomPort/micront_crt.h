#ifndef _MICRONT_CRT_H_
#define _MICRONT_CRT_H_
#include <stdarg.h>

typedef unsigned long long size_t;
typedef long long ssize_t;
typedef long off_t;
typedef int ptrdiff_t;
typedef int wchar_t;

#define NULL ((void*)0)
#define EOF (-1)

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

typedef unsigned long uintptr_t;
typedef long intptr_t;
typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;
typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;
typedef int int_fast8_t;
typedef long int_fast16_t;
typedef long int_fast32_t;
typedef long long int_fast64_t;
typedef unsigned long uint_fast8_t;
typedef unsigned long uint_fast16_t;
typedef unsigned long uint_fast32_t;
typedef unsigned long long uint_fast64_t;
typedef long long intmax_t;
typedef unsigned long long uintmax_t;

#define INT8_MIN   (-128)
#define INT16_MIN  (-32768)
#define INT32_MIN  (-2147483647-1)
#define INT64_MIN  (-9223372036854775807LL-1)
#define INT8_MAX   127
#define INT16_MAX  32767
#define INT32_MAX  2147483647
#define INT64_MAX  9223372036854775807LL
#define UINT8_MAX  255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL
#define SIZE_MAX   ((size_t)-1)
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#define PTRDIFF_MIN INT64_MIN
#define PTRDIFF_MAX INT64_MAX

#define INT8_C(x)   (x)
#define INT16_C(x)  (x)
#define INT32_C(x)  (x)
#define INT64_C(x)  (x##LL)
#define UINT8_C(x)  (x)
#define UINT16_C(x) (x)
#define UINT32_C(x) (x##U)
#define UINT64_C(x) (x##ULL)
#define INTMAX_C(x)  (x##LL)
#define UINTMAX_C(x) (x##ULL)



int sscanf(const char* str, const char* fmt, ...);
double atof(const char* s);
double strtod(const char* s, char** end);
int rename(const char* oldpath, const char* newpath);
int mkdir(const char* path, int mode);
int system(const char* cmd);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct _FILE FILE;
typedef long fpos_t;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* fp);
int feof(FILE* fp);
int ferror(FILE* fp);
int fflush(FILE* fp);
int fgetc(FILE* fp);
char* fgets(char* buf, int n, FILE* fp);
int fputc(int c, FILE* fp);
int fputs(const char* s, FILE* fp);
size_t fread(void* buf, size_t size, size_t count, FILE* fp);
size_t fwrite(const void* buf, size_t size, size_t count, FILE* fp);
int fseek(FILE* fp, long offset, int whence);
long ftell(FILE* fp);
void rewind(FILE* fp);
int fprintf(FILE* fp, const char* fmt, ...);
int fscanf(FILE* fp, const char* fmt, ...);
int vfprintf(FILE* fp, const char* fmt, va_list ap);
void setbuf(FILE* fp, char* buf);
void clearerr(FILE* fp);
int ungetc(int c, FILE* fp);

#define O_RDONLY 0
#define O_RDWR   2

typedef int (*cmp_func)(const void*, const void*);

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t count, size_t size);

void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);

size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
char* strtok(char* str, const char* delim);
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strdup(const char* s);
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, size_t n);

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isxdigit(int c);
int iscntrl(int c);
int isgraph(int c);
int toupper(int c);
int tolower(int c);

int atoi(const char* s);
long atol(const char* s);
long strtol(const char* s, char** end, int base);
int abs(int n);
long labs(long n);

int printf(const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int fprintf(void* fp, const char* fmt, ...);
int fputs(const char* s, void* fp);
int puts(const char* s);
int putchar(int c);

int open(const char* path, int flags, ...);
int read(int fd, void* buf, unsigned int count);
int write(int fd, const void* buf, unsigned int count);
int close(int fd);
long lseek(int fd, long offset, int whence);
int access(const char* path, int mode);
int remove(const char* path);
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, cmp_func cmp);

void qsort(void* base, size_t nmemb, size_t size, cmp_func cmp);
int rand(void);
void srand(unsigned int seed);
unsigned long time(void* t);
void exit(int code);
void abort(void);
char* getenv(const char* name);
int atexit(void (*fn)(void));
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
extern int errno;

#define EISDIR 21
#define ENOTDIR 20
#define ENOENT 2
#define EEXIST 17
#define EACCES 13
#define EINVAL 22
#define ENOSPC 28
#define ERANGE 34

#define RAND_MAX 32767

#define INT_MAX 2147483647
#define INT_MIN (-2147483647-1)
#define LONG_MAX 2147483647L
#define LONG_MIN (-2147483647L-1)
#define ULONG_MAX 4294967295UL
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-9223372036854775807LL-1)
#define ULLONG_MAX 18446744073709551615ULL
#define CHAR_BIT 8
#define CHAR_MAX 127
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MAX 32767
#define SHRT_MIN (-32768)
#define USHRT_MAX 65535
#define MB_LEN_MAX 1
#define FLT_RADIX 2
#define DBL_MAX 1.7976931348623158e+308
#define FLT_MAX 3.402823466e+38F

#define true 1
#define false 0
typedef int bool;

#endif
