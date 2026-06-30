#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "debug.h"
#include "runtime.h"

#define HEAP_BLOCK_MAGIC 0x48454150ULL
#define HEAP_MIN_BLOCK   16
#define HEAP_ALIGN       16

typedef struct HeapBlock {
    u64  magic;
    u64  size;
    i32  free;
    u32  pad;
    struct HeapBlock* next;
    struct HeapBlock* prev;
} HeapBlock;

static HeapBlock* g_heap_head;
static u64        g_heap_total;
static u64        g_heap_used;

static void HeapExpand(u64 min_size) {
    u64 pages = (min_size + sizeof(HeapBlock) + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 phys = PmmAllocPages(pages);
    if (!phys) KdPanic("Heap: Out of memory expanding heap");

    u64 total_bytes = pages * PAGE_SIZE;
    HeapBlock* block = (HeapBlock*)PHYS_TO_VIRT(phys);
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = total_bytes - sizeof(HeapBlock);
    block->free = 1;
    block->next = NULL;
    block->prev = NULL;
    block->pad = 0;

    if (g_heap_head) {
        HeapBlock* last = g_heap_head;
        while (last->next) last = last->next;
        block->prev = last;
        last->next = block;
    } else {
        g_heap_head = block;
    }

    g_heap_total += total_bytes;
}

void HeapInit(void) {
    g_heap_head = NULL;
    g_heap_total = 0;
    g_heap_used = 0;

    HeapExpand(PAGE_SIZE * 4);

    KdPrintf("[HEAP] Initialized: %llu KB total\n", g_heap_total / 1024);
}

static void HeapSplitBlock(HeapBlock* block, u64 size) {
    if (block->size >= size + sizeof(HeapBlock) + HEAP_MIN_BLOCK) {
        HeapBlock* new_block = (HeapBlock*)((u8*)block + sizeof(HeapBlock) + size);
        new_block->magic = HEAP_BLOCK_MAGIC;
        new_block->size = block->size - size - sizeof(HeapBlock);
        new_block->free = 1;
        new_block->next = block->next;
        new_block->prev = block;
        new_block->pad = 0;

        if (block->next) block->next->prev = new_block;
        block->next = new_block;
        block->size = size;
    }
}

static void HeapCoalesce(HeapBlock* block) {
    if (block->next && block->next->magic == HEAP_BLOCK_MAGIC && block->next->free) {
        HeapBlock* next = block->next;
        block->size += sizeof(HeapBlock) + next->size;
        block->next = next->next;
        if (next->next) next->next->prev = block;
    }
    if (block->prev && block->prev->magic == HEAP_BLOCK_MAGIC && block->prev->free) {
        HeapBlock* prev = block->prev;
        prev->size += sizeof(HeapBlock) + block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
    }
}

void* KmAlloc(u64 size) {
    if (size == 0) return NULL;

    size = (size + HEAP_ALIGN - 1) & ~(u64)(HEAP_ALIGN - 1);

    HeapBlock* block = g_heap_head;
    while (block) {
        if (block->magic != HEAP_BLOCK_MAGIC) {
            KdPanic("Heap: Corrupted block detected!");
        }
        if (block->free && block->size >= size) {
            HeapSplitBlock(block, size);
            block->free = 0;
            g_heap_used += block->size;
            return (void*)((u8*)block + sizeof(HeapBlock));
        }
        block = block->next;
    }

    HeapExpand(size);
    return KmAlloc(size);
}

void KmFree(void* ptr) {
    if (!ptr) return;

    HeapBlock* block = (HeapBlock*)((u8*)ptr - sizeof(HeapBlock));
    if (block->magic != HEAP_BLOCK_MAGIC) {
        KdPanic("Heap: Invalid free! Bad magic");
    }
    if (block->free) {
        KdPanic("Heap: Double free detected!");
    }

    block->free = 1;
    g_heap_used -= block->size;
    HeapCoalesce(block);
}
