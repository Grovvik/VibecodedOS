#ifndef _KERNEL_ARCH_IDT_H_
#define _KERNEL_ARCH_IDT_H_

#include "types.h"
#include "cpu.h"

#pragma pack(push, 1)
typedef struct {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} IdtEntry;
#pragma pack(pop)

#define IDT_ENTRIES 256

typedef void (*IrqHandler)(TrapFrame* frame);

void KeInitIdt(void);
void KeSetIrqHandler(u8 irq, IrqHandler handler);
void IdtSetEntry(i32 idx, void* handler, u16 selector, u8 ist, u8 type_attr);
void IsrHandler(TrapFrame* frame);

#endif
