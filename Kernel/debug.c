#include "debug.h"
#include "hal.h"
#include "runtime.h"
#include "serial.h"
#include <stdarg.h>
#include <intrin.h>

#define COM1_BASE 0x3F8
#define DEBUGCON_PORT 0xE9

static i32 g_kd_initialized = 0;

void KdInit(void) {
    SerialInit();
    g_kd_initialized = 1;
    KdPrintString("[KD] Debug output initialized (COM1 + debugcon)\n");
}

void KdPutChar(char c) {
    if (!g_kd_initialized) {
        HalOutByte(DEBUGCON_PORT, (u8)c);
        return;
    }
    SerialPutChar(c);
    HalOutByte(DEBUGCON_PORT, (u8)c);
}

void KdPrintString(const char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') KdPutChar('\r');
        KdPutChar(*s++);
    }
}

#define KD_PRINTF_BUF_SIZE 512

void KvPrintf(const char* fmt, va_list args) {
    char buf[KD_PRINTF_BUF_SIZE];
    char* p = buf;
    const char* f = fmt;

    while (*f && (p - buf) < (KD_PRINTF_BUF_SIZE - 2)) {
        if (*f != '%') {
            *p++ = *f++;
            continue;
        }
        f++;
        i32 zero_pad = 0;
        i32 width = 0;
        i32 is_long = 0;

        if (*f == '0') { zero_pad = 1; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        if (*f == 'l') { is_long = 1; f++; }
        if (*f == 'l') { is_long = 2; f++; }

        switch (*f) {
        case '%': *p++ = '%'; break;
        case 'c': {
            char c = (char)va_arg(args, int);
            *p++ = c;
            break;
        }
        case 's': {
            const char* s = va_arg(args, const char*);
            if (!s) s = "(null)";
            while (*s && (p - buf) < (KD_PRINTF_BUF_SIZE - 2)) *p++ = *s++;
            break;
        }
        case 'd': case 'i': {
            i64 val;
            if (is_long >= 2) val = va_arg(args, i64);
            else if (is_long == 1) val = va_arg(args, long);
            else val = va_arg(args, int);
            char num[22];
            RtItoa(val, num, 10);
            i32 len = (i32)RtStrLen(num);
            if (val < 0) len--;
            while (len < width) { *p++ = zero_pad ? '0' : ' '; len++; }
            const char* n = num;
            while (*n && (p - buf) < (KD_PRINTF_BUF_SIZE - 2)) *p++ = *n++;
            break;
        }
        case 'u': {
            u64 val;
            if (is_long >= 2) val = va_arg(args, u64);
            else if (is_long == 1) val = va_arg(args, unsigned long);
            else val = va_arg(args, unsigned int);
            char num[22];
            RtUtoa(val, num, 10);
            i32 len = (i32)RtStrLen(num);
            while (len < width) { *p++ = zero_pad ? '0' : ' '; len++; }
            const char* n = num;
            while (*n && (p - buf) < (KD_PRINTF_BUF_SIZE - 2)) *p++ = *n++;
            break;
        }
        case 'x': case 'X': case 'p': {
            u64 val;
            if (*f == 'p') {
                val = (u64)(usize)va_arg(args, void*);
                *p++ = '0'; *p++ = 'x';
                width = 16; zero_pad = 1;
            } else {
                if (is_long >= 2) val = va_arg(args, u64);
                else if (is_long == 1) val = va_arg(args, unsigned long);
                else val = va_arg(args, unsigned int);
            }
            char num[18];
            RtUtoa(val, num, 16);
            i32 len = (i32)RtStrLen(num);
            while (len < width) { *p++ = zero_pad ? '0' : ' '; len++; }
            const char* n = num;
            while (*n && (p - buf) < (KD_PRINTF_BUF_SIZE - 2)) *p++ = *n++;
            break;
        }
        default: *p++ = '%'; *p++ = *f; break;
        }
        f++;
    }
    *p = 0;
    KdPrintString(buf);
}

void KdPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    KvPrintf(fmt, args);
    va_end(args);
}

void KdPanic(const char* msg, ...) {
    va_list args;
    va_start(args, msg);

    KdPrintString("\n\n*** KERNEL PANIC ***\n");
    KdPrintString("Message: ");
    KvPrintf(msg, args);
    KdPrintString("\n*** SYSTEM HALTED ***\n");

    va_end(args);

    KdBreak();
    HalCli();
    while (1) {
        HalHlt();
    }
}

void KdDumpHex(const void* data, usize size) {
    const u8* bytes = (const u8*)data;
    for (usize i = 0; i < size; i += 16) {
        KdPrintf("%p: ", (void*)(bytes + i));
        for (usize j = 0; j < 16 && (i + j) < size; j++) {
            KdPrintf("%02x ", bytes[i + j]);
        }
        KdPrintString("\n");
    }
}

void KdBreak(void) {
    __debugbreak();
}
