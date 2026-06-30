#ifndef _KERNEL_DEBUG_H_
#define _KERNEL_DEBUG_H_

#include "types.h"
#include <stdarg.h>

void KdInit(void);
void KdPutChar(char c);
void KdPrintString(const char* s);
void KvPrintf(const char* fmt, va_list args);
void KdPrintf(const char* fmt, ...);
void KdPanic(const char* msg, ...);
void KdDumpHex(const void* data, usize size);
void KdBreak(void);

#define KD_ASSERT(cond) do { if (!(cond)) KdPanic("ASSERT FAIL: " #cond " at %s:%d", __FILE__, __LINE__); } while(0)
#define KD_ENTER() KdPrintf("[DBG] Entered %s\n", __FUNCTION__)
#define KD_LEAVE() KdPrintf("[DBG] Leaving %s\n", __FUNCTION__)

#endif
