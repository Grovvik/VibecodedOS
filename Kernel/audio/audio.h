#ifndef _KERNEL_AUDIO_AUDIO_H_
#define _KERNEL_AUDIO_AUDIO_H_

#include "types.h"

void AudioInit(void);
i32  AudioInitialized(void);
ntstatus AudioPlay(const void* buffer, u32 size);
void AudioStop(void);

#endif
