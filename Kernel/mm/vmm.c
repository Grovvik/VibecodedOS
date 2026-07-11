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
    PmmPinPage(pml4_phys);  /* Kernel PML4 must never be freed */
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

static u64* GetNextLevel(u64* current_level, u32 index, u64 flags, int pin) {
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

    /* Pin every newly-allocated kernel page-table page so PMM can never
       reclaim them (prevents the use-after-free that corrupts shared PTEs). */
    if (pin) PmmPinPage(phys);

    return virt;
}

void VmmMapPage(u64* pml4, u64 virt, u64 phys, u64 flags) {
    int pin = (pml4 == g_kernel_pml4) ? 1 : 0;
    u64* pdpt = GetNextLevel(pml4, PML4E_INDEX(virt), flags, pin);
    if (!pdpt) return;
    
    u64* pd = GetNextLevel(pdpt, PDPTE_INDEX(virt), flags, pin);
    if (!pd) return;
    
    u64* pt = GetNextLevel(pd, PDE_INDEX(virt), flags, pin);
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
    
    /* Share kernel's higher-half mappings (PML4[256..511]) */
    for (i32 i = 256; i < 512; i++) {
        pml4[i] = g_kernel_pml4[i];
    }

    /* Identity-map low memory + kernel image + framebuffer + PCI range so
       that kernel interrupt handlers (which run at low virtual addresses)
       remain reachable when this process's CR3 is active.  These per-process
       page table pages are separate from the kernel's own structural pages;
       the kernel's structural pages are pinned by VmmPinKernelPageTables so
       PmmFreePage will never release them even if they are encountered by
       PsFreeAddressSpace. */
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

/* Walk the kernel's PML4 and pin every intermediate page-table page
   (PDPT / PD / PT) so PmmFreePage will silently ignore any attempt to
   release them.  Call this once, immediately after VmmInit(). */
void VmmPinKernelPageTables(void) {
    u64 pml4_phys = (u64)(usize)g_kernel_pml4 - PHYS_OFFSET;
    PmmPinPage(pml4_phys);

    for (i32 i = 0; i < 512; i++) {
        if (!(g_kernel_pml4[i] & VMM_PRESENT)) continue;
        u64 pdpt_phys = g_kernel_pml4[i] & PAGE_MASK;
        PmmPinPage(pdpt_phys);
        u64* pdpt = (u64*)PHYS_TO_VIRT(pdpt_phys);

        for (i32 j = 0; j < 512; j++) {
            if (!(pdpt[j] & VMM_PRESENT)) continue;
            if (pdpt[j] & VMM_HUGE) continue;
            u64 pd_phys = pdpt[j] & PAGE_MASK;
            PmmPinPage(pd_phys);
            u64* pd = (u64*)PHYS_TO_VIRT(pd_phys);

            for (i32 k = 0; k < 512; k++) {
                if (!(pd[k] & VMM_PRESENT)) continue;
                if (pd[k] & VMM_HUGE) continue;
                u64 pt_phys = pd[k] & PAGE_MASK;
                PmmPinPage(pt_phys);
                /* data pages (pointed to by PTEs) are NOT pinned */
            }
        }
    }
    KdPrintf("[VMM] Kernel page-table pages pinned\n");
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