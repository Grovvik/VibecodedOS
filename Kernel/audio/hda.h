#ifndef _KERNEL_AUDIO_HDA_H_
#define _KERNEL_AUDIO_HDA_H_

#include "types.h"

void HdaInit(void);
i32  HdaInitialized(void);
ntstatus HdaPlayBuffer(const void* buffer, u32 size);
void HdaStop(void);
void HdaPoll(void);

#endif
