#ifndef _KERNEL_MM_PMM_H_
#define _KERNEL_MM_PMM_H_

#include "types.h"

void     PmmInit(void* memory_map, u64 map_size, u64 entry_size, u64 entry_count);
u64      PmmAllocPage(void);
u64      PmmAllocPageDebug(const char* caller);
u64      PmmAllocPages(u64 count);
void     PmmFreePage(u64 phys_addr);
void     PmmFreePages(u64 phys_addr, u64 count);
u64      PmmGetTotalPages(void);
u64      PmmGetUsedPages(void);
u64      PmmGetFreePages(void);
u64      PmmGetHighestPage(void);
int      PmmIsPageTracked(u64 phys_addr);
void     PmmPinPage(u64 phys_addr);    /* Pin a page so PmmFreePage will never release it */

#endif