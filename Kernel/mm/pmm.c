#include "pmm.h"
#include "debug.h"
#include "runtime.h"
#include "hal.h"
#include "boot_info.h"

#define PMM_BITMAP_SIZE_MAX 131072

static u64  g_pmm_total_pages;
static u64  g_pmm_used_pages;
static u64  g_pmm_highest_page;
static u64  g_pmm_bitmap[PMM_BITMAP_SIZE_MAX];
static u32  g_pmm_bitmap_size;

#define BITMAP_INDEX(page) ((u32)((page) / 64))
#define BITMAP_OFFSET(page) ((u32)((page) % 64))
#define BITMAP_SET(page)   (g_pmm_bitmap[BITMAP_INDEX(page)] |= (1ULL << BITMAP_OFFSET(page)))
#define BITMAP_CLEAR(page) (g_pmm_bitmap[BITMAP_INDEX(page)] &= ~(1ULL << BITMAP_OFFSET(page)))
#define BITMAP_TEST(page)  ((g_pmm_bitmap[BITMAP_INDEX(page)] >> BITMAP_OFFSET(page)) & 1)

static void PmmMarkRegionUsed(u64 base, u64 length) {
    u64 start = base / PAGE_SIZE;
    u64 end = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (u64 i = start; i < end && i <= g_pmm_highest_page; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_pmm_used_pages++;
        }
    }
}

void PmmInit(void* memory_map, u64 map_size, u64 entry_size, u64 entry_count) {
    g_pmm_total_pages = 0;
    g_pmm_used_pages = 0;
    g_pmm_highest_page = 0;

    u64 max_addr = 0;

    for (u64 i = 0; i < entry_count; i++) {
        void* desc_ptr = (u8*)memory_map + i * entry_size;
        u32 type = *(u32*)((u8*)desc_ptr + 0);
        u64 phys_start = *(u64*)((u8*)desc_ptr + 8);
        u64 num_pages_val = *(u64*)((u8*)desc_ptr + 24);
        u64 region_end = phys_start + num_pages_val * PAGE_SIZE;

        if (type == 7 || type == 4 || type == 3) {
            if (region_end > max_addr) max_addr = region_end;
            g_pmm_total_pages += num_pages_val;
        }
    }

    g_pmm_highest_page = max_addr / PAGE_SIZE;
    g_pmm_bitmap_size = (u32)((g_pmm_highest_page + 63) / 64);

    if (g_pmm_bitmap_size > PMM_BITMAP_SIZE_MAX) {
        KdPanic("PMM: Bitmap too large! Increase PMM_BITMAP_SIZE_MAX");
    }

    RtMemSet(g_pmm_bitmap, 0xFF, g_pmm_bitmap_size * sizeof(u64));
    g_pmm_used_pages = g_pmm_highest_page;

    for (u64 i = 0; i < entry_count; i++) {
        void* desc_ptr = (u8*)memory_map + i * entry_size;
        u32 type = *(u32*)((u8*)desc_ptr + 0);
        u64 phys_start = *(u64*)((u8*)desc_ptr + 8);
        u64 num_pages_val = *(u64*)((u8*)desc_ptr + 24);

        if (type == 7 || type == 4 || type == 3) {
            u64 start = phys_start / PAGE_SIZE;
            u64 end = start + num_pages_val;

            for (u64 j = start; j < end && j <= g_pmm_highest_page; j++) {
                if (BITMAP_TEST(j)) {
                    BITMAP_CLEAR(j);
                    g_pmm_used_pages--;
                }
            }
        }
    }

    extern BootInfo* g_boot_info;
    PmmMarkRegionUsed(g_boot_info->kernel_image_base, g_boot_info->kernel_image_size);
    PmmMarkRegionUsed((u64)(usize)memory_map, map_size);
    PmmMarkRegionUsed(g_boot_info->fb_base, g_boot_info->fb_size);

    KdPrintf("[PMM] Kernel image: 0x%llx - 0x%llx\n",
             g_boot_info->kernel_image_base,
             g_boot_info->kernel_image_base + g_boot_info->kernel_image_size);
    KdPrintf("[PMM] Bitmap array: 0x%llx - 0x%llx\n",
             (u64)&g_pmm_bitmap,
             (u64)&g_pmm_bitmap + sizeof(g_pmm_bitmap));

    if (!BITMAP_TEST(0)) {
        BITMAP_SET(0);
        g_pmm_used_pages++;
    }

    KdPrintf("[PMM] Initialized: %llu total pages, %llu used, %llu free (bitmap %u qwords)\n",
             g_pmm_total_pages, g_pmm_used_pages,
             g_pmm_total_pages - g_pmm_used_pages, g_pmm_bitmap_size);
}

u64 PmmAllocPage(void) {
    for (u64 i = 0; i < g_pmm_highest_page; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_pmm_used_pages++;
            /* KdPrintf("[PMM] AllocPage: 0x%llx (page %llu)\n", i * PAGE_SIZE, i); */
            return i * PAGE_SIZE;
        }
    }
    KdPrintf("[PMM] OUT OF MEMORY!\n");
    return 0;
}

u64 PmmAllocPageDebug(const char* caller) {
    for (u64 i = 0; i < g_pmm_highest_page; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_pmm_used_pages++;
            KdPrintf("[PMM] AllocPage: 0x%llx (page %llu) from %s\n", i * PAGE_SIZE, i, caller);
            return i * PAGE_SIZE;
        }
    }
    KdPrintf("[PMM] OUT OF MEMORY! (from %s)\n", caller);
    return 0;
}

u64 PmmAllocPages(u64 count) {
    if (count == 0) return 0;
    if (count == 1) return PmmAllocPage();

    u64 found = 0;
    u64 start = 0;

    for (u64 i = 0; i < g_pmm_highest_page; i++) {
        if (!BITMAP_TEST(i)) {
            if (found == 0) start = i;
            found++;
            if (found == count) {
                for (u64 j = start; j < start + count; j++) {
                    BITMAP_SET(j);
                }
                g_pmm_used_pages += count;
                KdPrintf("[PMM] AllocPages(%llu): 0x%llx - 0x%llx\n",
                         count, start * PAGE_SIZE, (start + count) * PAGE_SIZE);
                return start * PAGE_SIZE;
            }
        } else {
            found = 0;
        }
    }

    KdPrintf("[PMM] Cannot allocate %llu contiguous pages!\n", count);
    return 0;
}

void PmmFreePage(u64 phys_addr) {
    if (phys_addr == 0) return;
    u64 page = phys_addr / PAGE_SIZE;
    if (page > g_pmm_highest_page) {
        KdPrintf("[PMM] FreePage: 0x%llx exceeds highest page %llu!\n", phys_addr, g_pmm_highest_page);
        return;
    }
    if (!BITMAP_TEST(page)) {
        KdPrintf("[PMM] FreePage: 0x%llx (page %llu) already free! Double-free detected\n", phys_addr, page);
        return;
    }
    BITMAP_CLEAR(page);
    g_pmm_used_pages--;
}

void PmmFreePages(u64 phys_addr, u64 count) {
    for (u64 i = 0; i < count; i++) {
        PmmFreePage(phys_addr + i * PAGE_SIZE);
    }
}

u64 PmmGetTotalPages(void) { return g_pmm_total_pages; }
u64 PmmGetUsedPages(void) { return g_pmm_used_pages; }
u64 PmmGetFreePages(void) { return g_pmm_total_pages - g_pmm_used_pages; }
u64 PmmGetHighestPage(void) { return g_pmm_highest_page; }

int PmmIsPageTracked(u64 phys_addr) {
    u64 page = phys_addr / PAGE_SIZE;
    return (page < g_pmm_highest_page) ? 1 : 0;
}
