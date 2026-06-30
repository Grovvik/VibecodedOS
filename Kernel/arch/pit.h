#ifndef _KERNEL_ARCH_PIT_H_
#define _KERNEL_ARCH_PIT_H_

#include "types.h"
#include "cpu.h"

void KeInitPit(u32 freq);
void PitIrqHandler(TrapFrame* frame);
u64  KeGetTickCount(void);
void KeSleepTicks(u64 ticks);

#endif
