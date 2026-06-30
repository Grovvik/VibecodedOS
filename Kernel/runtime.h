#ifndef _KERNEL_RUNTIME_H_
#define _KERNEL_RUNTIME_H_

#include "types.h"

void  RtMemSet(void* dst, u8 val, usize count);
void  RtMemCopy(void* dst, const void* src, usize count);
void  RtMemMove(void* dst, const void* src, usize count);
i32   RtMemCompare(const void* a, const void* b, usize count);

usize RtStrLen(const char* s);
usize RtStrnLen(const char* s, usize max);
char* RtStrCopy(char* dst, const char* src);
char* RtStrnCopy(char* dst, const char* src, usize max);
i32   RtStrCompare(const char* a, const char* b);
i32   RtStrnCompare(const char* a, const char* b, usize max);
i32   RtStrCompareI(const char* a, const char* b);

char* RtStrConcat(char* dst, const char* src);

void  RtItoa(i64 value, char* buf, i32 base);
void  RtUtoa(u64 value, char* buf, i32 base);

#endif
