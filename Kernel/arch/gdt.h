#ifndef _KERNEL_ARCH_GDT_H_
#define _KERNEL_ARCH_GDT_H_

#include "types.h"

#pragma pack(push, 1)
typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  flags_limit_hi;
    u8  base_high;
} GdtEntry;

typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  flags_limit_hi;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} TssDescriptor;

typedef struct {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[8];
    u32 reserved2;
    u32 reserved3;
    u16 reserved4;
    u16 iomap_base;
} Tss64;
#pragma pack(pop)

#define GDT_ENTRIES 9

void KeInitGdt(void);
Tss64* KeGetTss(void);
void KeSetTssRsp0(u64 rsp);

#endif
