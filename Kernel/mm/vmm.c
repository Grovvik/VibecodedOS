#include "vmm.h"
#include "pmm.h"
#include "debug.h"
#include "runtime.h"
#include "hal.h"
#include "boot_info.h"

extern BootInfo* g_boot_info;

u8 g_vmm_active = 0;
static u64* g_kernel_pml4;

#define PTE_INDEX(virt) (((virt) >> 12) & 0x1FF)
#define PDE_INDEX(virt) (((virt) >> 21) & 0x1FF)
#define PDPTE_INDEX(virt) (((virt) >> 30) & 0x1FF)
#define PML4E_INDEX(virt) (((virt) >> 39) & 0x1FF)

static void Invlpg(u64 virt) {
    __invlpg((void*)(usize)virt);
}

void VmmInit(void) {
    u64 pml4_phys = PmmAllocPage();
    g_kernel_pml4 = (u64*)PHYS_TO_VIRT(pml4_phys);
    RtMemSet(g_kernel_pml4, 0, PAGE_SIZE);

    u64 total_ram = PmmGetHighestPage() * PAGE_SIZE;
    u64 pages = (total_ram + PAGE_SIZE - 1) / PAGE_SIZE;
    KdPrintf("[VMM] Mapping %llu pages of physical RAM to higher half\n", pages);
    
    for (u64 i = 0; i < pages; i++) {
        VmmMapPage(g_kernel_pml4, PHYS_OFFSET + i * PAGE_SIZE, i * PAGE_SIZE, VMM_KERNEL_FLAGS | VMM_NX);
    }

    u64 kernel_start = g_boot_info->kernel_image_base & PAGE_MASK;
    u64 kernel_pages = (g_boot_info->kernel_image_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < kernel_pages; i++) {
        VmmMapPage(g_kernel_pml4, kernel_start + i * PAGE_SIZE, kernel_start + i * PAGE_SIZE, VMM_KERNEL_FLAGS);
    }
    
    u64 fb_start = g_boot_info->fb_base & PAGE_MASK;
    u64 fb_pages = (g_boot_info->fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < fb_pages; i++) {
        VmmMapPage(g_kernel_pml4, fb_start + i * PAGE_SIZE, fb_start + i * PAGE_SIZE, VMM_KERNEL_FLAGS | VMM_NX);
    }

    for (u64 i = 0xF0000000; i < 0x100000000ULL; i += PAGE_SIZE) {
        VmmMapPage(g_kernel_pml4, i, i, VMM_KERNEL_FLAGS | VMM_NX);
    }
    
    for (u64 i = 0; i < 0x100000; i += PAGE_SIZE) {
        VmmMapPage(g_kernel_pml4, i, i, VMM_KERNEL_FLAGS);
    }

    u64 mem_map_start = (u64)(usize)g_boot_info->memory_map & PAGE_MASK;
    u64 mem_map_pages = (g_boot_info->memory_map_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < mem_map_pages; i++) {
        VmmMapPage(g_kernel_pml4, mem_map_start + i * PAGE_SIZE, mem_map_start + i * PAGE_SIZE, VMM_KERNEL_FLAGS | VMM_NX);
    }

    __writecr3(pml4_phys);
    g_vmm_active = 1;
    g_boot_info = (BootInfo*)((u64)(usize)g_boot_info + PHYS_OFFSET);
    KdPrintf("[VMM] VMM Initialized, CR3 = 0x%llx\n", pml4_phys);
}

u64* VmmGetPml4(void) {
    return g_kernel_pml4;
}

static u64* GetNextLevel(u64* current_level, u32 index, u64 flags) {
    if (current_level[index] & VMM_PRESENT) {
        if (flags & VMM_USER) {
            current_level[index] |= VMM_USER;
        }
        return (u64*)PHYS_TO_VIRT(current_level[index] & PAGE_MASK);
    }
    u64 phys = PmmAllocPage();
    if (!phys) return NULL;
    u64* virt = (u64*)PHYS_TO_VIRT(phys);
    RtMemSet(virt, 0, PAGE_SIZE);
    
    u64 dir_flags = VMM_PRESENT | VMM_WRITABLE;
    if (flags & VMM_USER) dir_flags |= VMM_USER;
    
    current_level[index] = phys | dir_flags;
    return virt;
}

void VmmMapPage(u64* pml4, u64 virt, u64 phys, u64 flags) {
    u64* pdpt = GetNextLevel(pml4, PML4E_INDEX(virt), flags);
    if (!pdpt) return;
    
    u64* pd = GetNextLevel(pdpt, PDPTE_INDEX(virt), flags);
    if (!pd) return;
    
    u64* pt = GetNextLevel(pd, PDE_INDEX(virt), flags);
    if (!pt) return;
    
    pt[PTE_INDEX(virt)] = (phys & PAGE_MASK) | flags;
    Invlpg(virt);
}

void VmmMapPages(u64* pml4, u64 virt, u64 phys, u64 count, u64 flags) {
    for (u64 i = 0; i < count; i++) {
        VmmMapPage(pml4, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags);
    }
}

u64 VmmUnmapPage(u64* pml4, u64 virt) {
    if (!(pml4[PML4E_INDEX(virt)] & VMM_PRESENT)) return 0;
    u64* pdpt = (u64*)PHYS_TO_VIRT(pml4[PML4E_INDEX(virt)] & PAGE_MASK);
    
    if (!(pdpt[PDPTE_INDEX(virt)] & VMM_PRESENT)) return 0;
    u64* pd = (u64*)PHYS_TO_VIRT(pdpt[PDPTE_INDEX(virt)] & PAGE_MASK);
    
    if (!(pd[PDE_INDEX(virt)] & VMM_PRESENT)) return 0;
    if (pd[PDE_INDEX(virt)] & VMM_HUGE) {
        u64 phys = (pd[PDE_INDEX(virt)] & PAGE_MASK);
        pd[PDE_INDEX(virt)] = 0;
        Invlpg(virt);
        return phys;
    }
    u64* pt = (u64*)PHYS_TO_VIRT(pd[PDE_INDEX(virt)] & PAGE_MASK);
    
    if (!(pt[PTE_INDEX(virt)] & VMM_PRESENT)) return 0;
    
    u64 phys = pt[PTE_INDEX(virt)] & PAGE_MASK;
    pt[PTE_INDEX(virt)] = 0;
    Invlpg(virt);
    return phys;
}

void VmmUnmapPages(u64* pml4, u64 virt, u64 count) {
    for (u64 i = 0; i < count; i++) {
        VmmUnmapPage(pml4, virt + i * PAGE_SIZE);
    }
}

u64 VmmGetPhysical(u64* pml4, u64 virt) {
    if (!(pml4[PML4E_INDEX(virt)] & VMM_PRESENT)) return 0;
    u64* pdpt = (u64*)PHYS_TO_VIRT(pml4[PML4E_INDEX(virt)] & PAGE_MASK);
    
    if (!(pdpt[PDPTE_INDEX(virt)] & VMM_PRESENT)) return 0;
    u64* pd = (u64*)PHYS_TO_VIRT(pdpt[PDPTE_INDEX(virt)] & PAGE_MASK);
    
    if (!(pd[PDE_INDEX(virt)] & VMM_PRESENT)) return 0;
    if (pd[PDE_INDEX(virt)] & VMM_HUGE) {
        return (pd[PDE_INDEX(virt)] & PAGE_MASK) + (virt & 0x1FFFFF);
    }
    
    u64* pt = (u64*)PHYS_TO_VIRT(pd[PDE_INDEX(virt)] & PAGE_MASK);
    if (!(pt[PTE_INDEX(virt)] & VMM_PRESENT)) return 0;
    
    return (pt[PTE_INDEX(virt)] & PAGE_MASK) + (virt & 0xFFF);
}

u64* VmmCreateAddressSpace(void) {
    u64 pml4_phys = PmmAllocPage();
    if (!pml4_phys) return NULL;
    
    u64* pml4 = (u64*)PHYS_TO_VIRT(pml4_phys);
    RtMemSet(pml4, 0, PAGE_SIZE);
    
    for (i32 i = 256; i < 512; i++) {
        pml4[i] = g_kernel_pml4[i];
    }
    
    for (u64 i = 0; i < 0x100000; i += PAGE_SIZE) {
        VmmMapPage(pml4, i, i, VMM_KERNEL_FLAGS);
    }

    u64 kernel_start = g_boot_info->kernel_image_base & PAGE_MASK;
    u64 kernel_pages = (g_boot_info->kernel_image_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < kernel_pages; i++) {
        VmmMapPage(pml4, kernel_start + i * PAGE_SIZE, kernel_start + i * PAGE_SIZE, VMM_KERNEL_FLAGS);
    }
    
    u64 fb_start = g_boot_info->fb_base & PAGE_MASK;
    u64 fb_pages = (g_boot_info->fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < fb_pages; i++) {
        VmmMapPage(pml4, fb_start + i * PAGE_SIZE, fb_start + i * PAGE_SIZE, VMM_KERNEL_FLAGS | VMM_NX);
    }

    for (u64 i = 0xF0000000; i < 0x100000000ULL; i += PAGE_SIZE) {
        VmmMapPage(pml4, i, i, VMM_KERNEL_FLAGS | VMM_NX);
    }

    return pml4;
}

void VmmSwitchAddressSpace(u64 pml4_phys) {
    __writecr3(pml4_phys);
}

void VmmIdentityMapRange(u64* pml4, u64 phys, u64 size, u64 flags) {
    u64 start = phys & PAGE_MASK;
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < pages; i++) {
        VmmMapPage(pml4, start + i * PAGE_SIZE, start + i * PAGE_SIZE, flags);
    }
}

void VmmMapKernelInto(u64* new_pml4) {
    for (i32 i = 256; i < 512; i++) {
        new_pml4[i] = g_kernel_pml4[i];
    }
}

void VmmDumpPte(u64* pml4, u64 virt) {
    KdPrintf("[VMM] Dump virt 0x%llx:\n", virt);
    u64 pml4e = pml4[PML4E_INDEX(virt)];
    KdPrintf("  PML4E: 0x%llx\n", pml4e);
    if (!(pml4e & VMM_PRESENT)) return;
    
    u64* pdpt = (u64*)PHYS_TO_VIRT(pml4e & PAGE_MASK);
    u64 pdpte = pdpt[PDPTE_INDEX(virt)];
    KdPrintf("  PDPTE: 0x%llx\n", pdpte);
    if (!(pdpte & VMM_PRESENT)) return;
    
    u64* pd = (u64*)PHYS_TO_VIRT(pdpte & PAGE_MASK);
    u64 pde = pd[PDE_INDEX(virt)];
    KdPrintf("  PDE:   0x%llx\n", pde);
    if (!(pde & VMM_PRESENT)) return;
    if (pde & VMM_HUGE) return;
    
    u64* pt = (u64*)PHYS_TO_VIRT(pde & PAGE_MASK);
    u64 pte = pt[PTE_INDEX(virt)];
    KdPrintf("  PTE:   0x%llx\n", pte);
}