#include "gdt.h"
#include "hal.h"
#include "debug.h"
#include "runtime.h"

static GdtEntry      g_gdt[GDT_ENTRIES];
static TssDescriptor g_tss_desc;
static Tss64         g_tss;

u64 g_tss_rsp0_ptr;

static void GdtSetEntry(i32 idx, u32 base, u32 limit, u8 access, u8 flags) {
    GdtEntry* e = &g_gdt[idx];
    e->limit_low    = (u16)(limit & 0xFFFF);
    e->base_low     = (u16)(base & 0xFFFF);
    e->base_mid     = (u8)((base >> 16) & 0xFF);
    e->access       = access;
    e->flags_limit_hi = (u8)((flags << 4) | ((limit >> 16) & 0x0F));
    e->base_high    = (u8)((base >> 24) & 0xFF);
}

static void GdtSetTssEntry(i32 idx, u64 base, u32 limit) {
    TssDescriptor* t = &g_tss_desc;
    t->limit_low    = (u16)(limit & 0xFFFF);
    t->base_low     = (u16)(base & 0xFFFF);
    t->base_mid     = (u8)((base >> 16) & 0xFF);
    t->access       = 0x89;
    t->flags_limit_hi = (u8)((limit >> 16) & 0x0F);
    t->base_high    = (u8)((base >> 24) & 0xFF);
    t->base_upper   = (u32)((base >> 32) & 0xFFFFFFFF);
    t->reserved     = 0;
    g_gdt[idx]     = *(GdtEntry*)&t[0];
    g_gdt[idx + 1] = *(GdtEntry*)&t[1];
}

void KeInitGdt(void) {
    KdPrintf("[GDT] Initializing GDT...\n");

    RtMemSet(g_gdt, 0, sizeof(g_gdt));
    RtMemSet(&g_tss, 0, sizeof(g_tss));
    RtMemSet(&g_tss_desc, 0, sizeof(g_tss_desc));

    g_tss.iomap_base = sizeof(Tss64);
    g_tss_rsp0_ptr = (u64)&g_tss.rsp0;

    KdPrintf("[GDT] TSS at %p, size %u bytes\n", &g_tss, (u32)sizeof(Tss64));

    GdtSetEntry(0, 0, 0, 0, 0);
    GdtSetEntry(1, 0, 0xFFFFF, 0x9A, 0x0A);
    GdtSetEntry(2, 0, 0xFFFFF, 0x92, 0x0C);
    GdtSetEntry(3, 0, 0xFFFFF, 0xFA, 0x0A);
    GdtSetEntry(4, 0, 0xFFFFF, 0xF2, 0x0C);
    GdtSetEntry(5, 0, 0xFFFFF, 0xFA, 0x0A);

    KdPrintf("[GDT] Entry 5 access=0x%02x flags=0x%02x\n",
             g_gdt[5].access, g_gdt[5].flags_limit_hi);

    GdtSetTssEntry(7, (u64)(usize)&g_tss, sizeof(Tss64) - 1);

    u16 gdt_limit = (u16)(sizeof(g_gdt) - 1);

    KdPrintf("[GDT] Loading GDT at %p, limit %u\n", g_gdt, gdt_limit);
    HalLoadGdt(g_gdt, gdt_limit);

    KdPrintf("[GDT] Reloading segment registers...\n");
    HalReloadSegments();

    KdPrintf("[GDT] Loading Task Register (selector 0x38)...\n");
    HalLoadTr(0x38);

    KdPrintf("[GDT] GDT initialized OK\n");
}

Tss64* KeGetTss(void) {
    return &g_tss;
}

void KeSetTssRsp0(u64 rsp) {
    g_tss.rsp0 = rsp;
}
