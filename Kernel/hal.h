#ifndef _KERNEL_HAL_H_
#define _KERNEL_HAL_H_

#include "types.h"

u8   HalInByte(u16 port);
u16  HalInWord(u16 port);
u32  HalInDword(u16 port);
void HalOutByte(u16 port, u8 val);
void HalOutWord(u16 port, u16 val);
void HalOutDword(u16 port, u32 val);
void HalIoWait(void);

void HalCli(void);
void HalSti(void);
void HalHlt(void);

u64  HalReadCr3(void);
void HalWriteCr3(u64 val);
void HalInvlpg(void* addr);

u64  HalReadTsc(void);

void HalLoadGdt(void* gdt, u16 size);
void HalLoadIdt(void* idt, u16 size);
void HalLoadTr(u16 selector);
void HalReloadSegments(void);

extern void HalAsmCli(void);
extern void HalAsmSti(void);
extern u64  HalAsmDisableInterrupts(void);
extern void HalAsmRestoreInterrupts(u64 state);

void HalSetKernelStack(u64 rsp);

u64  HalDisableInterrupts(void);
void HalRestoreInterrupts(u64 state);

typedef struct {
    u64 rbx, rcx, rdx;
} UserInitRegs;

void HalJumpToUser(u64 entry, u64 stack, u64 cr3, UserInitRegs* init);

#endif
