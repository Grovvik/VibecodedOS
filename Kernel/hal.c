#include "hal.h"
#include <intrin.h>

extern void HalAsmLoadGdt(void* descriptor);
extern void HalAsmLoadIdt(void* descriptor);
extern void HalAsmLoadTr(u16 selector);
extern void HalAsmReloadSegments(void);
extern void HalAsmSetRsp0(u64* location, u64 value);
extern void HalAsmCli(void);
extern void HalAsmSti(void);
extern void HalAsmJumpToUser(u64 entry, u64 stack, u64 cr3, UserInitRegs* init);

u8 HalInByte(u16 port) {
    return __inbyte(port);
}

u16 HalInWord(u16 port) {
    return __inword(port);
}

u32 HalInDword(u16 port) {
    return __indword(port);
}

void HalOutByte(u16 port, u8 val) {
    __outbyte(port, val);
}

void HalOutWord(u16 port, u16 val) {
    __outword(port, val);
}

void HalOutDword(u16 port, u32 val) {
    __outdword(port, val);
}

void HalIoWait(void) {
    HalOutByte(0x80, 0);
}

void HalCli(void) {
    HalAsmCli();
}

void HalSti(void) {
    HalAsmSti();
}

void HalHlt(void) {
    __halt();
}

u64 HalReadCr3(void) {
    return __readcr3();
}

void HalWriteCr3(u64 val) {
    __writecr3(val);
}

void HalInvlpg(void* addr) {
    __invlpg(addr);
}

u64 HalReadTsc(void) {
    return __rdtsc();
}

u64 HalDisableInterrupts(void) {
    return HalAsmDisableInterrupts();
}

void HalRestoreInterrupts(u64 state) {
    HalAsmRestoreInterrupts(state);
}

#pragma pack(push, 1)
typedef struct {
    u16 limit;
    u64 base;
} PseudoDescriptor;
#pragma pack(pop)

void HalLoadGdt(void* gdt, u16 size) {
    PseudoDescriptor desc;
    desc.limit = size;
    desc.base = (u64)gdt;
    HalAsmLoadGdt(&desc);
}

void HalLoadIdt(void* idt, u16 size) {
    PseudoDescriptor desc;
    desc.limit = size;
    desc.base = (u64)idt;
    HalAsmLoadIdt(&desc);
}

void HalLoadTr(u16 selector) {
    HalAsmLoadTr(selector);
}

void HalReloadSegments(void) {
    HalAsmReloadSegments();
}

void HalJumpToUser(u64 entry, u64 stack, u64 cr3, UserInitRegs* init) {
    HalAsmJumpToUser(entry, stack, cr3, init);
}

void HalSetKernelStack(u64 rsp) {
    extern u64 g_tss_rsp0_ptr;
    u64* rsp0 = (u64*)(usize)g_tss_rsp0_ptr;
    if (rsp0) *rsp0 = rsp;
}
