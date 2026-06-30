#ifndef _KERNEL_MM_VMM_H_
#define _KERNEL_MM_VMM_H_

#include "types.h"
#include "pmm.h"

#define VMM_PRESENT   (1ULL << 0)
#define VMM_WRITABLE  (1ULL << 1)
#define VMM_USER      (1ULL << 2)
#define VMM_ACCESSED  (1ULL << 5)
#define VMM_DIRTY     (1ULL << 6)
#define VMM_NX        (1ULL << 63)
#define VMM_HUGE      (1ULL << 7)

#define VMM_KERNEL_FLAGS  (VMM_PRESENT | VMM_WRITABLE)
#define VMM_USER_FLAGS    (VMM_PRESENT | VMM_WRITABLE | VMM_USER)

#define PHYS_OFFSET 0xFFFF800000000000ULL
#define PAGE_MASK 0x000FFFFFFFFFF000ULL

extern u8 g_vmm_active;
#define PHYS_TO_VIRT(p) (g_vmm_active ? ((p) + PHYS_OFFSET) : (p))

void VmmInit(void);

u64* VmmGetPml4(void);
u64* VmmCreateAddressSpace(void);
void VmmSwitchAddressSpace(u64 pml4_phys);

void VmmMapPage(u64* pml4, u64 virt, u64 phys, u64 flags);
void VmmMapPages(u64* pml4, u64 virt, u64 phys, u64 count, u64 flags);
u64  VmmUnmapPage(u64* pml4, u64 virt);
void VmmUnmapPages(u64* pml4, u64 virt, u64 count);

u64  VmmGetPhysical(u64* pml4, u64 virt);

void VmmIdentityMapRange(u64* pml4, u64 phys, u64 size, u64 flags);

void VmmMapKernelInto(u64* new_pml4);

void VmmDumpPte(u64* pml4, u64 virt);

#endif