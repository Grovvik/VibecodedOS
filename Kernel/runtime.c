#include "runtime.h"

void* memcpy(void* dst, const void* src, usize count) {
    RtMemCopy(dst, src, count);
    return dst;
}

void* memset(void* dst, int val, usize count) {
    RtMemSet(dst, (u8)val, count);
    return dst;
}

void* memmove(void* dst, const void* src, usize count) {
    RtMemMove(dst, src, count);
    return dst;
}

void RtMemSet(void* dst, u8 val, usize count) {
    u8* d = (u8*)dst;
    while (count--) *d++ = val;
}

void RtMemCopy(void* dst, const void* src, usize count) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (count--) *d++ = *s++;
}

void RtMemMove(void* dst, const void* src, usize count) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if (d < s) {
        while (count--) *d++ = *s++;
    } else {
        d += count;
        s += count;
        while (count--) *--d = *--s;
    }
}

i32 RtMemCompare(const void* a, const void* b, usize count) {
    const u8* pa = (const u8*)a;
    const u8* pb = (const u8*)b;
    while (count--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

usize RtStrLen(const char* s) {
    usize len = 0;
    while (*s++) len++;
    return len;
}

usize RtStrnLen(const char* s, usize max) {
    usize len = 0;
    while (len < max && *s++) len++;
    return len;
}

char* RtStrCopy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* RtStrnCopy(char* dst, const char* src, usize max) {
    char* d = dst;
    while (max-- > 1 && (*d++ = *src++));
    *d = 0;
    return dst;
}

i32 RtStrCompare(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(const u8*)a - *(const u8*)b;
}

i32 RtStrnCompare(const char* a, const char* b, usize max) {
    while (max-- && *a && *a == *b) { a++; b++; }
    if (max == (usize)-1) return 0;
    return *(const u8*)a - *(const u8*)b;
}

static char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

i32 RtStrCompareI(const char* a, const char* b) {
    while (*a && *b) {
        i32 d = (i32)(u8)to_lower(*a) - (i32)(u8)to_lower(*b);
        if (d != 0) return d;
        a++; b++;
    }
    return (i32)(u8)*a - (i32)(u8)*b;
}

char* RtStrConcat(char* dst, const char* src) {
    char* d = dst + RtStrLen(dst);
    while ((*d++ = *src++));
    return dst;
}

static const char g_hex_upper[] = "0123456789ABCDEF";
static const char g_hex_lower[] = "0123456789abcdef";

void RtItoa(i64 value, char* buf, i32 base) {
    if (value < 0 && base == 10) {
        *buf++ = '-';
        value = -value;
    }
    RtUtoa((u64)value, buf, base);
}

void RtUtoa(u64 value, char* buf, i32 base) {
    const char* digits = g_hex_lower;
    char tmp[65];
    i32 i = 0;

    if (base == 'X' || base == -16) { digits = g_hex_upper; base = 16; }
    if (base == 'x' || base == 16) { if (base < 0) { digits = g_hex_upper; base = 16; } }
    if (base <= 0) base = 10;

    if (value == 0) { buf[0] = '0'; buf[1] = 0; return; }

    while (value > 0) {
        tmp[i++] = digits[value % base];
        value /= base;
    }
    while (--i >= 0) *buf++ = tmp[i];
    *buf = 0;
}
