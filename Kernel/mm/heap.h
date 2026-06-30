#ifndef _KERNEL_MM_HEAP_H_
#define _KERNEL_MM_HEAP_H_

#include "types.h"

void HeapInit(void);

void* KmAlloc(u64 size);
void  KmFree(void* ptr);

#endif